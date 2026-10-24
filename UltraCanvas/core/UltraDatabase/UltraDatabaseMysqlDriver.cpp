// core/UltraDatabase/UltraDatabaseMysqlDriver.cpp
// The MySQL / MariaDB driver, over MariaDB Connector/C (libmariadb, LGPL-2.1,
// linked dynamically - see Docs/Research/UltraDBAdminDesignProposal.md §9.1).
// It speaks to MariaDB and to MySQL servers alike.
//
// What it does for the callers' SQL to run unchanged:
//
//  1. **Parameters are bound, never written into the SQL.** A statement with
//     parameters is a server-side prepared statement (mysql_stmt_*), which
//     takes `?` as UltraDatabase writes it - no rewriting at all. Without
//     parameters the text protocol runs it, which takes what a prepared
//     statement cannot (several statements at once, and the odd statement the
//     server refuses to prepare).
//  2. **Types.** The integer types come back as Int, FLOAT and DOUBLE as
//     Double, a BINARY / VARBINARY / BLOB column as Blob, BIT as the Int its
//     bits make; DECIMAL stays its exact text, and so does an unsigned BIGINT
//     too large for an Int. Dates and times are their text, as MySQL writes
//     them. Each result column carries its source database, table and column
//     where the server reports them, so a grid knows the row it shows.
//  3. **TLS on by default and verified** (UltraDbTls::VerifyFull), as for
//     PostgreSQL. A local socket needs none.
//  4. **The password comes from UltraVault** and is not kept. Stopping a
//     query (KILL QUERY) and a cursor each need a second, authenticated
//     connection, so the connection keeps its configuration - the vault key,
//     not the password - and reads the password again when it opens one.
//  5. **LOAD DATA LOCAL is off.** With it on, a server can ask the client for
//     any file the program can read; a database tool must not hand them out.
//
// Built only when libmariadb is present (ULTRADATABASE_HAS_MYSQL); without it
// the driver is not registered, and opening a "mysql" connection says the
// driver is missing.
// Version: 0.2.0 - through an SSH tunnel (VerifyFull refused there: Connector/C checks the address)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDatabase/UltraDatabaseCore.h"
#include "UltraDatabase/UltraDatabasePlugins.h"
#include "UltraDatabase/UltraDatabaseValue.h"

#include "UltraDatabaseInternal.h"

#if defined(ULTRADATABASE_HAS_MYSQL)

#include <mysql.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(ULTRADATABASE_HAS_VAULT)
  #include "UltraVault/UltraVault.h"
#endif

namespace {

const char* const kDriverId = "mysql";
constexpr unsigned int kBinaryCharset = 63;    // the charset number of binary data

// ===== ERRORS =====

UltraDbResultCode MapErrno(unsigned int code) {
    switch (code) {
        case 1062:   // ER_DUP_ENTRY
        case 1586:   // ER_DUP_ENTRY_WITH_KEY_NAME
        case 1451:   // ER_ROW_IS_REFERENCED_2
        case 1452:   // ER_NO_REFERENCED_ROW_2
        case 1216: case 1217:
        case 1048:   // ER_BAD_NULL_ERROR
        case 1364:   // ER_NO_DEFAULT_FOR_FIELD
        case 3819:   // ER_CHECK_CONSTRAINT_VIOLATED (MySQL)
        case 4025:   // ER_CONSTRAINT_FAILED (MariaDB)
            return UltraDbResultCode::ConstraintViolation;
        case 1205:   // ER_LOCK_WAIT_TIMEOUT
        case 1213:   // ER_LOCK_DEADLOCK
            return UltraDbResultCode::Busy;
        case 1317:   // ER_QUERY_INTERRUPTED
        case 3024:   // ER_QUERY_TIMEOUT (MySQL)
        case 1969:   // ER_STATEMENT_TIMEOUT (MariaDB)
            return UltraDbResultCode::Cancelled;
        case 2002: case 2003: case 2005: case 2006: case 2013: case 2055:
            return UltraDbResultCode::ConnectionFailed;
        default:
            return UltraDbResultCode::QueryFailed;
    }
}

UltraDbResult Fail(unsigned int code, const char* message, const char* state, const std::string& context) {
    std::string text = context;
    if (message != nullptr && *message != '\0') text += std::string(": ") + message;
    UltraDbResult r = UltraDbResult::Error(MapErrno(code), text, kDriverId);
    if (state != nullptr && std::strcmp(state, "00000") != 0) r.sqlState = state;
    return r;
}

UltraDbResult ConnError(MYSQL* conn, const std::string& context) {
    return Fail(mysql_errno(conn), mysql_error(conn), mysql_sqlstate(conn), context);
}

UltraDbResult StmtError(MYSQL_STMT* stmt, const std::string& context) {
    // An error that arrives while rows are being read - a KILL QUERY, a lost
    // server - is left on the connection, not on the statement.
    if (mysql_stmt_errno(stmt) == 0 && stmt->mysql != nullptr && mysql_errno(stmt->mysql) != 0)
        return ConnError(stmt->mysql, context);
    return Fail(mysql_stmt_errno(stmt), mysql_stmt_error(stmt), mysql_stmt_sqlstate(stmt), context);
}

// ===== VALUES =====

bool IsIntegerType(enum_field_types type) {
    return type == MYSQL_TYPE_TINY || type == MYSQL_TYPE_SHORT || type == MYSQL_TYPE_INT24 ||
           type == MYSQL_TYPE_LONG || type == MYSQL_TYPE_LONGLONG || type == MYSQL_TYPE_YEAR;
}

bool IsBinary(const MYSQL_FIELD& field) {
    if (field.charsetnr != kBinaryCharset) return false;
    switch (field.type) {
        case MYSQL_TYPE_STRING: case MYSQL_TYPE_VAR_STRING: case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_TINY_BLOB: case MYSQL_TYPE_BLOB: case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB: case MYSQL_TYPE_GEOMETRY:
            return true;
        default:
            return false;   // a number or a date also reports the binary charset
    }
}

// BIT(n) arrives as its bytes, most significant first.
int64_t BitValue(const char* data, size_t length) {
    uint64_t v = 0;
    for (size_t i = 0; i < length && i < 8; ++i) v = (v << 8) | static_cast<unsigned char>(data[i]);
    return static_cast<int64_t>(v);
}

// One value from its text form (the text protocol, or a column fetched as a
// string), by the column's type.
UltraDbValue ValueFromText(const MYSQL_FIELD& field, const char* data, size_t length) {
    if (data == nullptr) return UltraDbValue();
    if (field.type == MYSQL_TYPE_BIT) return UltraDbValue(BitValue(data, length));
    if (IsIntegerType(field.type)) {
        const std::string text(data, length);
        if ((field.flags & UNSIGNED_FLAG) != 0) {
            const unsigned long long u = std::strtoull(text.c_str(), nullptr, 10);
            if (u > static_cast<unsigned long long>(std::numeric_limits<int64_t>::max()))
                return UltraDbValue(text);   // exact, as text
            return UltraDbValue(static_cast<int64_t>(u));
        }
        return UltraDbValue(static_cast<int64_t>(std::strtoll(text.c_str(), nullptr, 10)));
    }
    if (field.type == MYSQL_TYPE_FLOAT || field.type == MYSQL_TYPE_DOUBLE) {
        double value = 0.0;
        ultradb_internal::ParseDoubleClassic(std::string(data, length), value);
        return UltraDbValue(value);
    }
    if (IsBinary(field)) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(data);
        return UltraDbValue(std::vector<uint8_t>(bytes, bytes + length));
    }
    return UltraDbValue(std::string(data, length));
}

// The type's name as MySQL spells it, for a result column's declared type.
std::string TypeName(const MYSQL_FIELD& field) {
    const bool binary = field.charsetnr == kBinaryCharset;
    if ((field.flags & ENUM_FLAG) != 0) return "enum";
    if ((field.flags & SET_FLAG) != 0) return "set";
    switch (field.type) {
        case MYSQL_TYPE_TINY:        return "tinyint";
        case MYSQL_TYPE_SHORT:       return "smallint";
        case MYSQL_TYPE_INT24:       return "mediumint";
        case MYSQL_TYPE_LONG:        return "int";
        case MYSQL_TYPE_LONGLONG:    return "bigint";
        case MYSQL_TYPE_FLOAT:       return "float";
        case MYSQL_TYPE_DOUBLE:      return "double";
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:  return "decimal";
        case MYSQL_TYPE_YEAR:        return "year";
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_NEWDATE:     return "date";
        case MYSQL_TYPE_TIME:        return "time";
        case MYSQL_TYPE_DATETIME:    return "datetime";
        case MYSQL_TYPE_TIMESTAMP:   return "timestamp";
        case MYSQL_TYPE_BIT:         return "bit";
        case MYSQL_TYPE_JSON:        return "json";
        case MYSQL_TYPE_GEOMETRY:    return "geometry";
        case MYSQL_TYPE_STRING:      return binary ? "binary" : "char";
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_VARCHAR:     return binary ? "varbinary" : "varchar";
        case MYSQL_TYPE_TINY_BLOB:   return binary ? "tinyblob" : "tinytext";
        case MYSQL_TYPE_MEDIUM_BLOB: return binary ? "mediumblob" : "mediumtext";
        case MYSQL_TYPE_LONG_BLOB:   return binary ? "longblob" : "longtext";
        case MYSQL_TYPE_BLOB:        return binary ? "blob" : "text";
        default:                     return std::string();
    }
}

void DescribeColumns(MYSQL_FIELD* fields, unsigned int count, UltraDbResultSet& out) {
    std::vector<std::string> names;
    std::vector<UltraDbColumnInfo> info;
    names.reserve(count);
    info.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const MYSQL_FIELD& f = fields[i];
        names.emplace_back(f.name, f.name_length);
        UltraDbColumnInfo ci;
        ci.name = names.back();
        ci.declaredType = TypeName(f);
        // A column read straight from a table names it; an expression does not.
        if (f.org_table != nullptr && f.org_table_length > 0 && f.org_name != nullptr && f.org_name_length > 0) {
            ci.sourceSchema = f.db != nullptr ? std::string(f.db, f.db_length) : std::string();
            ci.sourceTable = std::string(f.org_table, f.org_table_length);
            ci.sourceColumn = std::string(f.org_name, f.org_name_length);
        }
        info.push_back(std::move(ci));
    }
    out.SetColumns(std::move(names));
    out.SetColumnInfo(std::move(info));
}

// ===== PREPARED STATEMENTS =====

// The parameters, bound for mysql_stmt_bind_param. Owns what the binds point at.
class BoundParams {
public:
    explicit BoundParams(const UltraDbParams& params) : binds_(params.size()) {
        ints_.resize(params.size());
        doubles_.resize(params.size());
        texts_.resize(params.size());
        lengths_.resize(params.size());
        for (size_t i = 0; i < params.size(); ++i) {
            const UltraDbValue& v = params[i];
            MYSQL_BIND& b = binds_[i];
            std::memset(&b, 0, sizeof(b));
            switch (v.Type()) {
                case UltraDbType::Null:
                    b.buffer_type = MYSQL_TYPE_NULL;
                    break;
                case UltraDbType::Boolean:
                case UltraDbType::Int:
                    ints_[i] = v.AsInt64();
                    b.buffer_type = MYSQL_TYPE_LONGLONG;
                    b.buffer = &ints_[i];
                    break;
                case UltraDbType::Double:
                    doubles_[i] = v.AsDouble();
                    b.buffer_type = MYSQL_TYPE_DOUBLE;
                    b.buffer = &doubles_[i];
                    break;
                case UltraDbType::Blob: {
                    const std::vector<uint8_t> bytes = v.AsBlob();
                    texts_[i].assign(bytes.begin(), bytes.end());
                    lengths_[i] = static_cast<unsigned long>(texts_[i].size());
                    b.buffer_type = MYSQL_TYPE_BLOB;
                    b.buffer = texts_[i].data();
                    b.buffer_length = lengths_[i];
                    b.length = &lengths_[i];
                    break;
                }
                case UltraDbType::Text:
                default:
                    texts_[i] = v.AsString();
                    lengths_[i] = static_cast<unsigned long>(texts_[i].size());
                    b.buffer_type = MYSQL_TYPE_STRING;
                    b.buffer = texts_[i].data();
                    b.buffer_length = lengths_[i];
                    b.length = &lengths_[i];
                    break;
            }
        }
    }

    MYSQL_BIND* Binds() { return binds_.empty() ? nullptr : binds_.data(); }

private:
    std::vector<MYSQL_BIND> binds_;
    std::vector<long long> ints_;
    std::vector<double> doubles_;
    std::vector<std::string> texts_;
    std::vector<unsigned long> lengths_;
};

// Reads a prepared statement's result rows. Integers and doubles land in
// buffers of their own; every other column is fetched as text (or bytes) of
// exactly its length, with mysql_stmt_fetch_column once the length is known.
class StmtRows {
public:
    explicit StmtRows(MYSQL_STMT* stmt) : stmt_(stmt) {}
    ~StmtRows() { if (meta_ != nullptr) mysql_free_result(meta_); }

    // After execute: the columns and the result binds. False with an error
    // when the statement returns rows and they cannot be bound; true and no
    // columns when it returns none.
    bool Begin(UltraDbResultSet& out, UltraDbResult& error) {
        meta_ = mysql_stmt_result_metadata(stmt_);
        if (meta_ == nullptr) return true;
        count_ = mysql_num_fields(meta_);
        fields_ = mysql_fetch_fields(meta_);
        DescribeColumns(fields_, count_, out);
        binds_.assign(count_, MYSQL_BIND{});
        ints_.assign(count_, 0);
        doubles_.assign(count_, 0.0);
        lengths_.assign(count_, 0);
        nulls_.assign(count_, 0);
        errors_.assign(count_, 0);
        for (unsigned int i = 0; i < count_; ++i) {
            MYSQL_BIND& b = binds_[i];
            std::memset(&b, 0, sizeof(b));
            b.length = &lengths_[i];
            b.is_null = &nulls_[i];
            b.error = &errors_[i];
            const MYSQL_FIELD& f = fields_[i];
            if (IsIntegerType(f.type)) {
                b.buffer_type = MYSQL_TYPE_LONGLONG;
                b.buffer = &ints_[i];
                b.is_unsigned = (f.flags & UNSIGNED_FLAG) != 0;
            } else if (f.type == MYSQL_TYPE_FLOAT || f.type == MYSQL_TYPE_DOUBLE) {
                b.buffer_type = MYSQL_TYPE_DOUBLE;
                b.buffer = &doubles_[i];
            } else {
                // Length first, bytes after (Next).
                b.buffer_type = IsBinary(f) || f.type == MYSQL_TYPE_BIT ? MYSQL_TYPE_BLOB : MYSQL_TYPE_STRING;
                b.buffer = nullptr;
                b.buffer_length = 0;
            }
        }
        if (mysql_stmt_bind_result(stmt_, binds_.data()) != 0) {
            error = StmtError(stmt_, "binding the result failed");
            return false;
        }
        return true;
    }

    bool HasRows() const { return meta_ != nullptr; }

    // 1: a row in `row`; 0: no more; -1: an error in `error`.
    int Next(std::vector<UltraDbValue>& row, UltraDbResult& error) {
        const int status = mysql_stmt_fetch(stmt_);
        if (status == MYSQL_NO_DATA) return 0;
        if (status == 1) {
            error = StmtError(stmt_, "fetch failed");
            return -1;
        }
        row.clear();
        row.reserve(count_);
        for (unsigned int i = 0; i < count_; ++i) {
            const MYSQL_FIELD& f = fields_[i];
            if (nulls_[i]) { row.emplace_back(); continue; }
            if (IsIntegerType(f.type)) {
                if (binds_[i].is_unsigned && static_cast<unsigned long long>(ints_[i]) >
                        static_cast<unsigned long long>(std::numeric_limits<int64_t>::max()))
                    row.emplace_back(std::to_string(static_cast<unsigned long long>(ints_[i])));
                else
                    row.emplace_back(static_cast<int64_t>(ints_[i]));
                continue;
            }
            if (f.type == MYSQL_TYPE_FLOAT || f.type == MYSQL_TYPE_DOUBLE) {
                row.emplace_back(doubles_[i]);
                continue;
            }
            std::string data(lengths_[i], '\0');
            if (lengths_[i] > 0) {
                MYSQL_BIND b;
                std::memset(&b, 0, sizeof(b));
                unsigned long length = 0;
                b.buffer_type = binds_[i].buffer_type;
                b.buffer = data.data();
                b.buffer_length = lengths_[i];
                b.length = &length;
                if (mysql_stmt_fetch_column(stmt_, &b, i, 0) != 0) {
                    error = StmtError(stmt_, "reading a column failed");
                    return -1;
                }
            }
            row.push_back(ValueFromText(f, data.data(), data.size()));
        }
        return 1;
    }

private:
    MYSQL_STMT* stmt_;
    MYSQL_RES* meta_ = nullptr;
    MYSQL_FIELD* fields_ = nullptr;
    unsigned int count_ = 0;
    std::vector<MYSQL_BIND> binds_;
    std::vector<long long> ints_;
    std::vector<double> doubles_;
    std::vector<unsigned long> lengths_;
    std::vector<my_bool> nulls_;
    std::vector<my_bool> errors_;
};

// ===== OPENING =====

void Wipe(std::string& s) {
    volatile char* p = s.data();
    for (size_t i = 0; i < s.size(); ++i) p[i] = '\0';
    s.clear();
}

// The password, from UltraVault; see the PostgreSQL driver's Passwort for why
// a literal password is not taken.
bool Password(const UltraDbConnectionConfig& config, std::string& out, std::string& error) {
    out.clear();
    if (config.credentials.empty()) return true;   // a socket's unix_socket login, or none
    std::string key = config.credentials;
    if (key.rfind("vault:", 0) == 0) key = key.substr(6);
#if defined(ULTRADATABASE_HAS_VAULT)
    if (!UltraVault::IsAvailable()) {
        error = "the password for '" + key + "' cannot be read: UltraVault is not open";
        return false;
    }
    UltraVault::SecretValue secret;
    const UltraVault::Result read = UltraVault::Get(key, secret);
    if (!read) {
        error = read.code == UltraVault::ResultCode::NotFound
            ? "the password for '" + key + "' is not in UltraVault"
            : "the password for '" + key + "' could not be read from UltraVault: " + read.message;
        return false;
    }
    out.assign(secret.bytes.begin(), secret.bytes.end());
    volatile uint8_t* b = secret.bytes.data();
    for (size_t i = 0; i < secret.bytes.size(); ++i) b[i] = 0;
    return true;
#else
    error = "credentials were requested ('" + config.credentials +
            "') but UltraVault is not built in, and a literal password in a "
            "configuration file is not supported";
    return false;
#endif
}

// A new physical connection for `config`, or nullptr with `error` set. Every
// session the driver opens - the connection itself, a cancel, a cursor's -
// comes through here, so each is made the same way.
MYSQL* OpenSession(const UltraDbConnectionConfig& config, bool multiStatements, UltraDbResult& error) {
    std::string password, problem;
    if (!Password(config, password, problem)) {
        error = UltraDbResult::Error(UltraDbResultCode::CredentialsUnavailable, problem, kDriverId);
        return nullptr;
    }
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        Wipe(password);
        error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "could not allocate a connection", kDriverId);
        return nullptr;
    }
    mysql_optionsv(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned int noLocalFiles = 0;
    mysql_optionsv(conn, MYSQL_OPT_LOCAL_INFILE, &noLocalFiles);
    unsigned int timeout = 15;
    my_bool yes = 1, no = 0;
    switch (config.tls) {
        case UltraDbTls::Disable:
            mysql_optionsv(conn, MYSQL_OPT_SSL_ENFORCE, &no);
            mysql_optionsv(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &no);
            break;
        case UltraDbTls::Prefer:
            // TLS when the server offers it, plaintext when it does not.
            mysql_ssl_set(conn, nullptr, nullptr, nullptr, nullptr, nullptr);
            mysql_optionsv(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &no);
            break;
        case UltraDbTls::Require:
            mysql_optionsv(conn, MYSQL_OPT_SSL_ENFORCE, &yes);
            mysql_optionsv(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &no);
            break;
        case UltraDbTls::VerifyFull:
            mysql_optionsv(conn, MYSQL_OPT_SSL_ENFORCE, &yes);
            mysql_optionsv(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &yes);
            break;
    }
    for (const auto& option : config.options) {
        if (option.first == "connect_timeout") timeout = static_cast<unsigned int>(std::atoi(option.second.c_str()));
        else if (option.first == "ssl_ca") mysql_optionsv(conn, MYSQL_OPT_SSL_CA, option.second.c_str());
        else if (option.first == "ssl_cert") mysql_optionsv(conn, MYSQL_OPT_SSL_CERT, option.second.c_str());
        else if (option.first == "ssl_key") mysql_optionsv(conn, MYSQL_OPT_SSL_KEY, option.second.c_str());
        else {
            Wipe(password);
            mysql_close(conn);
            error = UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                         "unknown option '" + option.first +
                                             "' (connect_timeout, ssl_ca, ssl_cert, ssl_key)", kDriverId);
            return nullptr;
        }
    }
    mysql_optionsv(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    // Read only by the server, not by convention, as for PostgreSQL.
    if (config.readOnly) mysql_optionsv(conn, MYSQL_INIT_COMMAND, "SET SESSION TRANSACTION READ ONLY");

    // A host that is a path is the server's socket file.
    const bool socket = !config.host.empty() && config.host[0] == '/';
    const char* host = config.host.empty() || socket ? "localhost" : config.host.c_str();
    const char* unixSocket = socket ? config.host.c_str() : nullptr;
    const unsigned long flags = multiStatements ? CLIENT_MULTI_STATEMENTS : 0;
    MYSQL* opened = mysql_real_connect(conn, host, config.user.empty() ? nullptr : config.user.c_str(),
                                       password.empty() ? nullptr : password.c_str(),
                                       config.database.empty() ? nullptr : config.database.c_str(),
                                       config.port > 0 ? static_cast<unsigned int>(config.port) : 0,
                                       unixSocket, flags);
    Wipe(password);
    if (opened == nullptr) {
        error = ConnError(conn, "connection failed");
        error.code = UltraDbResultCode::ConnectionFailed;
        mysql_close(conn);
        return nullptr;
    }
    error = UltraDbResult::Ok();
    return conn;
}

// ===== CONNECTION =====

class MysqlConnection;

class MysqlStatement : public IUltraDbStatement {
public:
    MysqlStatement(MysqlConnection* owner, MYSQL_STMT* stmt) : owner_(owner), stmt_(stmt) {}
    ~MysqlStatement() override;
    UltraDbResult Execute(const UltraDbParams& params, UltraDbResultSet& out) override;

private:
    MysqlConnection* owner_;
    MYSQL_STMT* stmt_;
};

// Rows already read, handed out a page at a time: the cursor inside the
// caller's transaction, which must see what the transaction has written and
// so runs on the connection itself.
class MysqlBufferedCursor : public IUltraDbCursor {
public:
    explicit MysqlBufferedCursor(UltraDbResultSet rows) : rows_(std::move(rows)) {}
    UltraDbResult Fetch(size_t maxRows, UltraDbResultSet& out, bool& exhausted) override {
        out.Clear();
        out.SetColumns(rows_.Columns());
        out.SetColumnInfo(rows_.ColumnInfo());
        for (size_t n = 0; n < maxRows && next_ < rows_.Size(); ++n, ++next_) {
            const UltraDbRow& row = rows_.Row(next_);
            std::vector<UltraDbValue> values;
            values.reserve(row.Size());
            for (size_t i = 0; i < row.Size(); ++i) values.push_back(row[i]);
            out.AddRow(std::move(values));
        }
        exhausted = next_ >= rows_.Size();
        return UltraDbResult::Ok();
    }

private:
    UltraDbResultSet rows_;
    size_t next_ = 0;
};

// Rows streamed from a session of the cursor's own: the server sends them as
// they are read, nothing is held, and the connection itself stays free for
// other statements between pages.
class MysqlStreamCursor : public IUltraDbCursor {
public:
    MysqlStreamCursor(MYSQL* session, MYSQL_STMT* stmt) : session_(session), stmt_(stmt), rows_(stmt) {}
    ~MysqlStreamCursor() override {
        if (stmt_ != nullptr) mysql_stmt_close(stmt_);
        if (session_ != nullptr) mysql_close(session_);
    }

    UltraDbResult Begin() {
        UltraDbResultSet columns;
        UltraDbResult error = UltraDbResult::Ok();
        if (!rows_.Begin(columns, error)) return error;
        columns_ = columns.Columns();
        info_ = columns.ColumnInfo();
        return UltraDbResult::Ok();
    }

    UltraDbResult Fetch(size_t maxRows, UltraDbResultSet& out, bool& exhausted) override {
        out.Clear();
        out.SetColumns(columns_);
        out.SetColumnInfo(info_);
        exhausted = done_;
        if (done_ || !rows_.HasRows()) { exhausted = true; return UltraDbResult::Ok(); }
        std::vector<UltraDbValue> row;
        UltraDbResult error = UltraDbResult::Ok();
        for (size_t n = 0; n < maxRows; ++n) {
            const int got = rows_.Next(row, error);
            if (got < 0) return error;
            if (got == 0) { done_ = true; break; }
            out.AddRow(row);
        }
        exhausted = done_;
        return UltraDbResult::Ok();
    }

private:
    MYSQL* session_;
    MYSQL_STMT* stmt_;
    StmtRows rows_;
    std::vector<std::string> columns_;
    std::vector<UltraDbColumnInfo> info_;
    bool done_ = false;
};

class MysqlConnection : public IUltraDbConnection {
public:
    MysqlConnection(MYSQL* conn, UltraDbConnectionConfig config)
        : conn_(conn), config_(std::move(config)), threadId_(mysql_thread_id(conn)) {}
    ~MysqlConnection() override {
        if (conn_ != nullptr) mysql_close(conn_);
    }

    std::string DriverName() const override { return kDriverId; }
    std::string BeginTransactionSql() const override { return "START TRANSACTION"; }
    std::string RowLockSuffix() const override { return " FOR UPDATE"; }

    // Backticks, a backtick inside doubled - what MySQL takes whatever the
    // sql_mode (double quotes name something only under ANSI_QUOTES).
    std::string QuoteIdentifier(const std::string& name) const override {
        std::string out = "`";
        for (char c : name) {
            if (c == '`') out.push_back('`');
            out.push_back(c);
        }
        out.push_back('`');
        return out;
    }

    UltraDbResult ListSchemas(std::vector<std::string>& out) override {
        return ultradb_internal::MysqlListSchemas(*this, out);
    }
    UltraDbResult ListObjects(const std::string& schema, std::vector<UltraDbObjectSummary>& out) override {
        return ultradb_internal::MysqlListObjects(*this, schema, out);
    }
    UltraDbResult DescribeTable(const std::string& schema, const std::string& table,
                                UltraDbTableSchema& out) override {
        return ultradb_internal::MysqlDescribeTable(*this, schema, table, out);
    }

    // Server administration, UltraDatabaseServerViews.cpp.
    UltraDbResult ServerSessions(UltraDbResultSet& out) override {
        return ultradb_internal::MysqlServerSessions(*this, out);
    }
    UltraDbResult ServerSettings(const std::string& filter, UltraDbResultSet& out) override {
        return ultradb_internal::MysqlServerSettings(*this, filter, out);
    }
    UltraDbResult ServerSizes(const std::string& schema, UltraDbResultSet& out) override {
        return ultradb_internal::MysqlServerSizes(*this, schema, out);
    }
    UltraDbResult StopSessionSql(int64_t id, bool endSession, std::string& sql) override {
        sql = ultradb_internal::MysqlStopSessionSql(id, endSession);
        return UltraDbResult::Ok();
    }
    UltraDbResult StopSession(int64_t id, bool endSession, bool& stopped) override {
        return ultradb_internal::MysqlStopSession(*this, id, endSession, static_cast<int64_t>(threadId_), stopped);
    }

    std::unique_ptr<IUltraDbStatement> Prepare(const std::string& sql, UltraDbResult& error) override {
        std::lock_guard<std::mutex> lk(mutex_);
        MYSQL_STMT* stmt = PrepareLocked(sql, error);
        if (stmt == nullptr) return nullptr;
        return std::unique_ptr<IUltraDbStatement>(new MysqlStatement(this, stmt));
    }

    UltraDbResult ExecuteDirect(const std::string& sql, const UltraDbParams& params,
                                UltraDbResultSet& out) override {
        out.Clear();
        std::lock_guard<std::mutex> lk(mutex_);
        if (params.empty()) return RunText(sql, out, nullptr);
        UltraDbResult error = UltraDbResult::Ok();
        MYSQL_STMT* stmt = PrepareLocked(sql, error);
        if (stmt == nullptr) return error;
        UltraDbResult r = ExecuteLocked(stmt, params, out);
        mysql_stmt_close(stmt);
        return r;
    }

    UltraDbResult ExecutePrepared(MYSQL_STMT* stmt, const UltraDbParams& params, UltraDbResultSet& out) {
        out.Clear();
        std::lock_guard<std::mutex> lk(mutex_);
        return ExecuteLocked(stmt, params, out);
    }

    void CloseStatement(MYSQL_STMT* stmt) {
        std::lock_guard<std::mutex> lk(mutex_);
        mysql_stmt_close(stmt);
    }

    // Each statement of the script on its own, cut as the mysql client cuts
    // it (DELIMITER included), under one hold of the connection.
    UltraDbResult ExecuteBatch(const std::string& sql, std::vector<UltraDbStatementResult>& out) override {
        out.clear();
        const std::vector<std::string> statements = ultradb_internal::MysqlStatements(sql);
        std::lock_guard<std::mutex> lk(mutex_);
        for (const std::string& statement : statements) {
            UltraDbStatementResult one;
            one.sql = statement;
            one.result = RunText(statement, one.rows, &one.returnsRows);
            const bool ok = one.result.success;
            out.push_back(std::move(one));
            if (!ok) return out.back().result;
        }
        return UltraDbResult::Ok();
    }

    // KILL QUERY from a session of its own: the running statement fails with
    // ER_QUERY_INTERRUPTED (Cancelled), the connection stays. Takes no lock.
    UltraDbResult Cancel() override {
        UltraDbResult error = UltraDbResult::Ok();
        MYSQL* session = OpenSession(config_, false, error);
        if (session == nullptr) return error;
        const std::string kill = "KILL QUERY " + std::to_string(threadId_);
        UltraDbResult r = UltraDbResult::Ok();
        if (mysql_real_query(session, kill.c_str(), static_cast<unsigned long>(kill.size())) != 0)
            r = ConnError(session, "cancel request failed");
        mysql_close(session);
        return r;
    }

    std::unique_ptr<IUltraDbCursor> OpenCursor(const std::string& sql, const UltraDbParams& params,
                                               UltraDbResult& error) override {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (InTransaction()) {
                // Inside the caller's transaction: on this connection, so the
                // rows include what the transaction wrote - read at once.
                UltraDbResultSet rows;
                if (params.empty()) {
                    error = RunText(sql, rows, nullptr);
                } else {
                    MYSQL_STMT* stmt = PrepareLocked(sql, error);
                    if (stmt == nullptr) return nullptr;
                    error = ExecuteLocked(stmt, params, rows);
                    mysql_stmt_close(stmt);
                }
                if (!error) return nullptr;
                error.affectedRows = 0;
                return std::unique_ptr<IUltraDbCursor>(new MysqlBufferedCursor(std::move(rows)));
            }
        }
        MYSQL* session = OpenSession(config_, false, error);
        if (session == nullptr) return nullptr;
        MYSQL_STMT* stmt = mysql_stmt_init(session);
        if (stmt == nullptr || mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
            error = stmt != nullptr ? StmtError(stmt, "prepare failed") : ConnError(session, "prepare failed");
            if (stmt != nullptr) mysql_stmt_close(stmt);
            mysql_close(session);
            return nullptr;
        }
        std::unique_ptr<MysqlStreamCursor> cursor(new MysqlStreamCursor(session, stmt));
        if (mysql_stmt_param_count(stmt) != params.size()) {
            error = UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                         "the statement takes " + std::to_string(mysql_stmt_param_count(stmt)) +
                                             " parameters, " + std::to_string(params.size()) + " given", kDriverId);
            return nullptr;
        }
        BoundParams bound(params);
        if ((!params.empty() && mysql_stmt_bind_param(stmt, bound.Binds()) != 0) || mysql_stmt_execute(stmt) != 0) {
            error = StmtError(stmt, "execute failed");
            return nullptr;
        }
        error = cursor->Begin();
        if (!error) return nullptr;
        return cursor;
    }

private:
    bool InTransaction() {
        unsigned int status = 0;
        mariadb_get_infov(conn_, MARIADB_CONNECTION_SERVER_STATUS, &status);
        return (status & SERVER_STATUS_IN_TRANS) != 0;
    }

    MYSQL_STMT* PrepareLocked(const std::string& sql, UltraDbResult& error) {
        MYSQL_STMT* stmt = mysql_stmt_init(conn_);
        if (stmt == nullptr) {
            error = ConnError(conn_, "prepare failed");
            return nullptr;
        }
        if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
            error = StmtError(stmt, "prepare failed");
            mysql_stmt_close(stmt);
            return nullptr;
        }
        error = UltraDbResult::Ok();
        return stmt;
    }

    // Execute a prepared statement and read its rows. Called with mutex_ held.
    UltraDbResult ExecuteLocked(MYSQL_STMT* stmt, const UltraDbParams& params, UltraDbResultSet& out) {
        if (mysql_stmt_param_count(stmt) != params.size())
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "the statement takes " + std::to_string(mysql_stmt_param_count(stmt)) +
                                            " parameters, " + std::to_string(params.size()) + " given", kDriverId);
        BoundParams bound(params);
        if (!params.empty() && mysql_stmt_bind_param(stmt, bound.Binds()) != 0)
            return StmtError(stmt, "binding the parameters failed");
        if (mysql_stmt_execute(stmt) != 0) return StmtError(stmt, "execute failed");
        UltraDbResult r = UltraDbResult::Ok();
        StmtRows rows(stmt);
        if (!rows.Begin(out, r)) {
            mysql_stmt_free_result(stmt);
            return r;
        }
        if (rows.HasRows()) {
            std::vector<UltraDbValue> row;
            int64_t count = 0;
            for (;;) {
                const int got = rows.Next(row, r);
                if (got < 0) { mysql_stmt_free_result(stmt); return r; }
                if (got == 0) break;
                out.AddRow(row);
                ++count;
            }
            r.affectedRows = count;
        } else {
            const my_ulonglong affected = mysql_stmt_affected_rows(stmt);
            r.affectedRows = affected == static_cast<my_ulonglong>(-1) ? 0 : static_cast<int64_t>(affected);
            r.lastInsertId = static_cast<int64_t>(mysql_stmt_insert_id(stmt));
        }
        mysql_stmt_free_result(stmt);
        // A CALL answers with more than one result; the rest are drained.
        while (mysql_stmt_more_results(stmt) && mysql_stmt_next_result(stmt) == 0) mysql_stmt_free_result(stmt);
        return r;
    }

    // SQL without parameters over the text protocol - several statements at
    // once if it holds several, the rows of the last that returned any kept.
    // Called with mutex_ held.
    UltraDbResult RunText(const std::string& sql, UltraDbResultSet& out, bool* returnsRows) {
        out.Clear();
        if (returnsRows != nullptr) *returnsRows = false;
        if (mysql_real_query(conn_, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
            return ConnError(conn_, "execute failed");
        UltraDbResult r = UltraDbResult::Ok();
        for (;;) {
            MYSQL_RES* res = mysql_store_result(conn_);
            if (res != nullptr) {
                UltraDbResultSet rows;
                const unsigned int count = mysql_num_fields(res);
                DescribeColumns(mysql_fetch_fields(res), count, rows);
                MYSQL_FIELD* fields = mysql_fetch_fields(res);
                int64_t n = 0;
                while (MYSQL_ROW data = mysql_fetch_row(res)) {
                    const unsigned long* lengths = mysql_fetch_lengths(res);
                    std::vector<UltraDbValue> row;
                    row.reserve(count);
                    for (unsigned int i = 0; i < count; ++i)
                        row.push_back(ValueFromText(fields[i], data[i], lengths[i]));
                    rows.AddRow(std::move(row));
                    ++n;
                }
                mysql_free_result(res);
                out = std::move(rows);
                r.affectedRows = n;
                r.lastInsertId = 0;
                if (returnsRows != nullptr) *returnsRows = true;
            } else if (mysql_field_count(conn_) != 0) {
                return ConnError(conn_, "reading the result failed");
            } else {
                r.affectedRows = static_cast<int64_t>(mysql_affected_rows(conn_));
                r.lastInsertId = static_cast<int64_t>(mysql_insert_id(conn_));
            }
            const int more = mysql_next_result(conn_);
            if (more == -1) break;
            if (more > 0) return ConnError(conn_, "execute failed");
        }
        return r;
    }

    MYSQL* conn_;
    UltraDbConnectionConfig config_;   // the vault key, never the password
    unsigned long threadId_;
    std::mutex mutex_;
};

MysqlStatement::~MysqlStatement() {
    if (owner_ != nullptr && stmt_ != nullptr) owner_->CloseStatement(stmt_);
}

UltraDbResult MysqlStatement::Execute(const UltraDbParams& params, UltraDbResultSet& out) {
    return owner_->ExecutePrepared(stmt_, params, out);
}

// ===== DRIVER =====

class MysqlDriver : public IUltraDbDriverPlugin {
public:
    std::string GetName() const override { return "MySQL / MariaDB (MariaDB Connector/C)"; }
    std::vector<std::string> GetDriverIds() const override { return { "mysql", "mariadb" }; }

    int DefaultPort() const override { return 3306; }

    // Connector/C checks a certificate against the address it connects to -
    // 127.0.0.1 through a tunnel - and has no way to name another: VerifyFull
    // would fail on every certificate issued for the database's name.
    UltraDbResult ThroughTunnel(UltraDbConnectionConfig& config, int localPort) const override {
        if (config.tls == UltraDbTls::VerifyFull)
            return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                        "through an SSH tunnel MySQL can check no certificate against the "
                                        "database's name: choose TLS without verification (Require), or none",
                                        kDriverId);
        config.host = "127.0.0.1";
        config.port = localPort;
        return UltraDbResult::Ok();
    }

    std::unique_ptr<IUltraDbConnection> Open(const UltraDbConnectionConfig& config, UltraDbResult& error) override {
        MYSQL* conn = OpenSession(config, true, error);
        if (conn == nullptr) return nullptr;
        return std::unique_ptr<IUltraDbConnection>(new MysqlConnection(conn, config));
    }
};

} // namespace

namespace ultradb_internal {

IUltraDbDriverPlugin* BuiltinMysqlDriver() {
    static MysqlDriver driver;
    static std::once_flag once;
    std::call_once(once, [] {
        // mysql_library_init is not thread-safe; the first call does it here,
        // once, before any connection is opened on any thread.
        mysql_library_init(0, nullptr, nullptr);
        UltraDatabase_RegisterDriver(&driver);
    });
    return &driver;
}

} // namespace ultradb_internal

#else  // !ULTRADATABASE_HAS_MYSQL

namespace ultradb_internal {

// Built without libmariadb: there is no driver, which a "mysql" connection is
// then told plainly.
IUltraDbDriverPlugin* BuiltinMysqlDriver() { return nullptr; }

} // namespace ultradb_internal

#endif
