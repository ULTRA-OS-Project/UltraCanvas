-- sample.sql — SQL syntax-highlighting sample for the UltraCanvas demo.
CREATE TABLE authors (
    id         INTEGER PRIMARY KEY,
    name       VARCHAR(120) NOT NULL,
    country    CHAR(2)      DEFAULT 'US'
);

CREATE TABLE books (
    id         INTEGER PRIMARY KEY,
    author_id  INTEGER NOT NULL REFERENCES authors(id),
    title      VARCHAR(200) NOT NULL,
    price      DECIMAL(8, 2) CHECK (price >= 0),
    published  DATE
);

INSERT INTO authors (id, name, country) VALUES
    (1, 'Ada Lovelace', 'GB'),
    (2, 'Grace Hopper', 'US');

SELECT a.name AS author,
       COUNT(b.id) AS book_count,
       COALESCE(SUM(b.price), 0) AS catalogue_value
FROM authors AS a
LEFT JOIN books AS b ON b.author_id = a.id
WHERE a.country IN ('US', 'GB')
GROUP BY a.name
HAVING COUNT(b.id) >= 0
ORDER BY catalogue_value DESC;
