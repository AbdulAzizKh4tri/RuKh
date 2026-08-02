CREATE TABLE users (
    id                         INTEGER PRIMARY KEY AUTOINCREMENT,
    email                      TEXT,
    name                       TEXT,
    age                        INTEGER,
    best_friend                INTEGER,
    mother                     INTEGER,
    password                   TEXT,
    created_at                 INTEGER NOT NULL DEFAULT (CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER)),
    updated_at                 INTEGER NOT NULL DEFAULT (CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER)),
    FOREIGN KEY (best_friend) REFERENCES users(id) ON DELETE SET NULL,
    FOREIGN KEY (mother)      REFERENCES users(id) ON DELETE SET NULL
);

CREATE TABLE posts (
    id                     INTEGER PRIMARY KEY AUTOINCREMENT,
    title                  TEXT NOT NULL,
    content                TEXT NOT NULL,
    user_id                INTEGER,
    created_at             INTEGER NOT NULL DEFAULT (CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER)),
    updated_at             INTEGER NOT NULL DEFAULT (CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER)),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE 
);

CREATE TRIGGER users_updated_at_touch
AFTER UPDATE ON users
BEGIN
    UPDATE users
    SET updated_at = (CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER))
    WHERE id = NEW.id;
END;

CREATE TRIGGER posts_updated_at_touch
AFTER UPDATE ON posts
BEGIN
    UPDATE posts
    SET updated_at = (CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER))
    WHERE id = NEW.id;
END;
