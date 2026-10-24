- **SSH tunnels.** A PostgreSQL or MySQL / MariaDB server behind a bastion
  is reached through an SSH tunnel: **Through an SSH tunnel** in the
  connection dialog, the SSH server, its user and how to sign in there - the
  SSH agent, a password, or a private key file with its passphrase, all kept
  in the vault. On the first connection the bastion's host key is shown and
  trusted only when you agree; a bastion that presents another key later is
  refused. The tree, Browse and the SQL console all go through the tunnel.
