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

CREATE TABLE post_like_user (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id  INTEGER NOT NULL,
    post_id  INTEGER NOT NULL,
    liked_at INTEGER NOT NULL DEFAULT (unixepoch()),

    UNIQUE (user_id, post_id),

    FOREIGN KEY (user_id)
        REFERENCES users(id)
        ON DELETE CASCADE,

    FOREIGN KEY (post_id)
        REFERENCES posts(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_post_like_user_user_id
    ON post_like_user(user_id);

CREATE INDEX idx_post_like_user_post_id
    ON post_like_user(post_id);

CREATE TABLE user_friend_user (
    id  INTEGER PRIMARY KEY AUTOINCREMENT,
    pkA INTEGER NOT NULL,
    pkB INTEGER NOT NULL,

    UNIQUE (pkA, pkB),

    FOREIGN KEY (pkA)
        REFERENCES users(id)
        ON DELETE CASCADE,

    FOREIGN KEY (pkB)
        REFERENCES users(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_user_friend_user_pkA
    ON user_friend_user(pkA);

CREATE INDEX idx_user_friend_user_pkB
    ON user_friend_user(pkB);

CREATE TABLE user_follows_user (
    id              INTEGER         PRIMARY KEY AUTOINCREMENT,
    follower_id     INTEGER         NOT NULL,
    followee_id     INTEGER         NOT NULL,
    random_num      INTEGER,
    str             TEXT,

    FOREIGN KEY (follower_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (followee_id) REFERENCES users(id) ON DELETE CASCADE,

    UNIQUE (follower_id, followee_id)
);

CREATE INDEX user_follows_user_follower_id_idx 
	ON user_follows_user (follower_id);
CREATE INDEX user_follows_user_followee_id_idx 
	ON user_follows_user (followee_id);
