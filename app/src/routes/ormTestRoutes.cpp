#include "models/Follow.hpp"
#include "routes/testRoutes.hpp"

#include <nlohmann/json.hpp>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/db/ScopedTransaction.hpp>
#include <rukh/orm/OrmConfig.hpp>

#include "TestRunner.hpp"
#include "models/Post.hpp"
#include "models/User.hpp"
#include "rukh/orm/SelectQuery.hpp"

void registerOrmTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool) {
  auto db = rukh::orm::OrmConfig::db;

  using namespace rukh;
  using namespace nlohmann;

  // -- ORM test routes -------------------------------
  // All live under /tests/orm/* and interact with the 'users' table

  // GET /tests/orm/single_test
  // Runs a fixed sequence of ORM checks. Each step is isolated: a failure records that
  // step as failed and the suite continues. Response is a JSON summary with a per-step
  // pass/fail + detail, and an overall `allPassed`.
  router.get("/tests/orm/single_test", [threadPool, db](const HttpRequest &request) -> Task<Response> {
    using namespace models;
    using namespace testutil;
    using Pu = Predicate<User>;
    using Pp = Predicate<Post>;

    TestRunner runner;

    // --- 0. Start from a known-empty table so the suite is idempotent ---
    co_await runner.run("cleanup", [db]() -> Task<void> {
      unwrap(co_await User::bulkDestroy({true}), "bulkDestroy");
      auto count = unwrap(co_await User::queryAll().count(), "count");
      expect(count == 0, "expected 0 rows after cleanup, got " + std::to_string(count));

      unwrap(co_await Post::bulkDestroy({true}), "bulkDestroy");
      count = unwrap(co_await Post::queryAll().count(), "count");
      expect(count == 0, "expected 0 rows after cleanup, got " + std::to_string(count));
    });

    // --- 1. Single insert, including a nullable field left unset ---
    User greg;
    greg.name = "greg";
    greg.email = "greg@example.com";
    greg.password = "secret";

    co_await runner.run("single insert", [&greg]() -> Task<void> {
      // save() doesn't mutate `alice` in place — capture the returned, DB-hydrated model.
      unwrap(co_await greg.save(), "save");
      expect(greg.id > 0, "id not populated after insert");
    });

    // --- 2. Single find, verify nullable round-trips as empty ---
    co_await runner.run("single find", [&greg]() -> Task<void> {
      auto found = unwrap(co_await User::find(greg.id), "find");
      expect(found.has_value(), "find() returned nullopt");
      expect(not found->age.has_value(), "age should be null, got " + std::to_string(found->age.value_or(-1)));
      expect(found->name == "greg", "name mismatch: " + found->name.value_or("<unknown>"));
    });

    // --- 3. Single update: set a previously-null field, null out a previously-set one ---
    co_await runner.run("single update", [&greg]() -> Task<void> {
      greg.age = 20;
      greg.password = std::nullopt;
      unwrap(co_await greg.save(), "save");

      auto found = unwrap(co_await User::find(greg.id), "find");
      expect(found.has_value() && found->age == 20, "age not updated");
      expect(not found->password.has_value(), "password should be null after update");
    });

    // --- 4. Single destroy ---
    co_await runner.run("single destroy", [&greg]() -> Task<void> {
      unwrap(co_await greg.destroy(), "destroy");
      auto count = unwrap(co_await User::queryAll().count(), "count");
      expect(count == 0, "expected 0 rows after destroy, got " + std::to_string(count));
    });

    // --- 5. Bulk insert ---
    co_await runner.run("bulk insert", []() -> Task<void> {
      std::vector<User> batch;
      batch.push_back({.name = "Alice", .email = "alice@example.com"});
      batch.push_back({.name = "Bob", .email = "bob@example.com"});
      batch.push_back({.name = "Joe", .email = "joe@example.com"});
      batch.push_back({.name = "John", .email = "john@example.com"});

      auto [insertedCount, insertedRows] = unwrap(co_await User::bulkInsert(batch), "bulkInsert");
      expect(insertedCount == 4, "expected 4 rows, got " + std::to_string(insertedCount));
    });

    // --- 6. Bulk update, verify it actually applied ---
    co_await runner.run("bulk update", []() -> Task<void> {
      User patch;
      patch.name = "megaJoe";
      patch.password = "1234";
      Pu pred = Pu::iContains(&User::name, "j");
      pred = pred and Pu(true);
      auto [updatedCount, updatedRows] =
          unwrap(co_await User::bulkUpdate(patch, std::tuple{&User::name, &User::password}, pred), "bulkUpdate");
      expect(updatedCount == 2, "expected 2 rows, got " + std::to_string(updatedCount));

      auto afterUpdate = unwrap(co_await User::filter(pred).select(), "get");
      for (auto &u : afterUpdate)
        expect(u.name == "megaJoe", "row id " + std::to_string(u.id) + " not updated");
    });

    // --- 7. Bulk destroy, verify table is empty again ---
    co_await runner.run("bulk destroy", []() -> Task<void> {
      auto [destroyedCount, destroyedRows] = unwrap(co_await User::bulkDestroy({true}), "bulkDestroy");
      expect(destroyedCount == 4, "expected 4 rows, got " + std::to_string(destroyedCount));
      auto count = unwrap(co_await User::queryAll().count(), "count");
      expect(count == 0, "expected 0 rows, got " + std::to_string(count));
    });

    // --- 8. Transaction commit ---
    User bob;
    bob.name = "bob";
    bob.email = "bob@example.com";
    bob.age = 12;
    bob.password = "secret";

    co_await runner.run("transaction commit", [db, &bob]() -> Task<void> {
      db::ScopedTransaction transaction(db);
      co_await transaction.begin();
      unwrap(co_await bob.save(&transaction), "save");
      auto res = co_await transaction.commit();
      expect(res, "commit() returned false");

      auto found = unwrap(co_await User::find(bob.id), "find");
      expect(found.has_value() && found->age == 12, "expected age to be 12 after commit");
    });

    // --- 9. Transaction rollback: change visible inside txn, invisible outside, reverted after rollback ---
    co_await runner.run("transaction rollback", [db, &bob]() -> Task<void> {
      bob.age = 6;
      {
        db::ScopedTransaction transaction(db);
        co_await transaction.begin();
        // Deliberately not reassigning `bob` here — the outer variable must keep
        // representing the pre-transaction state for the "outside" check below.

        auto res = co_await bob.save(&transaction);
        unwrap(res, "save inside transaction");

        auto outside = unwrap(co_await User::find(bob.id), "find outside transaction");
        expect(outside.has_value() && outside->age == 12, "expected age to still be 12 outside the transaction");

        auto inside = unwrap(co_await User::find(bob.id, &transaction), "find inside transaction");
        expect(inside.has_value() && inside->age == 6, "expected age to be 6 inside the transaction");

        expect(co_await transaction.rollback(), "rollback() returned false");
      }

      auto found = unwrap(co_await User::find(bob.id), "find");
      expect(found.has_value() && found->age == 12, "expected age to be 12 due to rollback");
    });

    // --- 10. Insert-then-rollback: demonstrates the documented persisted_/id staleness caveat ---
    co_await runner.run("insert then rollback", [db]() -> Task<void> {
      User carol;
      carol.name = "carol";
      carol.email = "carol@example.com";
      carol.password = "secret";

      int64_t carolId = 0;
      {
        db::ScopedTransaction transaction(db);
        co_await transaction.begin();
        unwrap(co_await carol.save(&transaction), "save");
        expect(carol.id > 0, "id not populated after insert inside transaction");
        expect(co_await transaction.rollback(), "rollback() returned false");
      }
      // carolId was populated by the insert even though the row never actually landed —
      // this is the caveat: any in-memory copy from before a rollback is stale.
      auto found = unwrap(co_await User::find(carolId), "find after rollback");
      expect(not found.has_value(), "expected row to not exist after rollback, but find() succeeded");
    });

    // --- 11. Leave the table empty for the next run ---
    co_await runner.run("final cleanup",
                        []() -> Task<void> { unwrap(co_await User::bulkDestroy({true}), "bulkDestroy"); });

    json summary = runner.toJson();
    int status = summary["allPassed"].get<bool>() ? 200 : 500;
    co_return HttpResponse(status, "application/json", summary.dump(2));
  });

  // ---------------------------------------------------------------------------
  // GET /tests/orm/relations_test
  // ---------------------------------------------------------------------------
  router.get("/tests/orm/relations_test", [db](const HttpRequest &request) -> Task<Response> {
    using namespace models;
    using namespace testutil;
    using Pu = Predicate<User>;
    using Pp = Predicate<Post>;
    using Pfl = Predicate<PostLike<User, Post>>;
    using Pf = Predicate<Follow<User>>;

    TestRunner runner;

    // =========================================================================
    // 0. Cleanup – start from a known-empty state
    // =========================================================================
    co_await runner.run("cleanup", [db]() -> Task<void> {
      // Order matters because of FKs / cascades
      unwrap(co_await PostLike<User, Post>::bulkDestroy({true}), "PostLike bulkDestroy");
      unwrap(co_await Follow<User>::bulkDestroy({true}), "Follow bulkDestroy");
      unwrap(co_await Post::bulkDestroy({true}), "Post bulkDestroy");
      unwrap(co_await User::bulkDestroy({true}), "User bulkDestroy");

      // Also wipe the default through table used by friendship
      // (DefaultThroughModel creates "user_friend_user")
      // If you have a direct way to clear it, use it; otherwise the cascade
      // from User destroy should be enough in most setups.
      expect(unwrap(co_await User::queryAll().count(), "count") == 0, "users not empty");
      expect(unwrap(co_await Post::queryAll().count(), "count") == 0, "posts not empty");
    });

    // =========================================================================
    // 1. Seed users
    // =========================================================================
    User alice, bob, charlie, diana;
    co_await runner.run("seed users", [&]() -> Task<void> {
      alice = {.name = "Alice", .email = "alice@example.com", .age = 30};
      bob = {.name = "Bob", .email = "bob@example.com", .age = 28};
      charlie = {.name = "Charlie", .email = "charlie@example.com", .age = 25};
      diana = {.name = "Diana", .email = "diana@example.com", .age = 22};

      unwrap(co_await alice.save(), "alice save");
      unwrap(co_await bob.save(), "bob save");
      unwrap(co_await charlie.save(), "charlie save");
      unwrap(co_await diana.save(), "diana save");

      expect(alice.id > 0 && bob.id > 0 && charlie.id > 0 && diana.id > 0, "ids not set");
    });

    // =========================================================================
    // 2. Many-to-one  (Post → User)  + reverse one-to-many
    // =========================================================================
    Post post1, post2;
    co_await runner.run("many-to-one Post.user + reverse related()", [&]() -> Task<void> {
      post1 = {.title = "Hello", .content = "world", .user = alice.id};
      post2 = {.title = "Second", .content = "post", .user = alice.id};

      unwrap(co_await post1.save(), "post1 save");
      unwrap(co_await post2.save(), "post2 save");

      // Forward: ref<>()
      auto author = unwrap(co_await post1.ref<User>().first(), "ref author");
      expect(author.has_value() && author->id == alice.id, "ref() did not return Alice");

      // Reverse: related<>()
      auto alicePosts = unwrap(co_await alice.related<Post>().select(), "related posts");
      expect(alicePosts.size() == 2, "expected 2 posts for Alice, got " + std::to_string(alicePosts.size()));

      // Explicit related name (should be the same)
      auto alicePostsNamed = unwrap(co_await alice.related<Post, "user_posts">().select(), "named related");
      expect(alicePostsNamed.size() == 2, "named related mismatch");
    });

    // =========================================================================
    // 3. Optional FK + OnDelete::SET_NULL
    // =========================================================================
    co_await runner.run("OnDelete::SET_NULL", [&]() -> Task<void> {
      // Destroy Alice – posts should survive with user_id = NULL
      unwrap(co_await alice.destroy(), "destroy alice");

      auto p1 = unwrap(co_await Post::find(post1.id), "find post1");
      auto p2 = unwrap(co_await Post::find(post2.id), "find post2");
      expect(p1.has_value() && not p1->user.has_value(), "post1.user should be null after SET_NULL");
      expect(p2.has_value() && not p2->user.has_value(), "post2.user should be null after SET_NULL");

      // Re-create Alice so later tests have her
      alice = {.name = "Alice", .email = "alice@example.com", .age = 30};
      unwrap(co_await alice.save(), "recreate alice");
    });

    // =========================================================================
    // 4. Self-referential many-to-one  (bestFriend / mother)
    // =========================================================================
    co_await runner.run("self-ref many-to-one (bestFriend + mother)", [&]() -> Task<void> {
      bob.bestFriend = alice.id;
      bob.mother = diana.id;
      unwrap(co_await bob.save(), "bob update");

      // Forward
      auto best = unwrap(co_await bob.ref<User, &User::bestFriend>().first(), "ref bestFriend");
      expect(best.has_value() && best->id == alice.id, "bestFriend mismatch");

      auto mom = unwrap(co_await bob.ref<User, &User::mother>().first(), "ref mother");
      expect(mom.has_value() && mom->id == diana.id, "mother mismatch");

      // Reverse (relatedName)
      auto bestFriendsOfAlice = unwrap(co_await alice.related<User, "best_friends">().select(), "best_friends");
      expect(bestFriendsOfAlice.size() == 1 && bestFriendsOfAlice[0].id == bob.id,
             "expected Bob as best_friend of Alice");

      auto childrenOfDiana = unwrap(co_await diana.related<User, "children">().select(), "children");
      expect(childrenOfDiana.size() == 1 && childrenOfDiana[0].id == bob.id, "expected Bob as child of Diana");
    });

    // =========================================================================
    // 5. Many-to-many – likes (Post ↔ User via PostLike) asymmetric
    // =========================================================================
    co_await runner.run("M2M likes – upsert / manyRelated / remove", [&]() -> Task<void> {
      // Re-assign posts to Alice
      post1.user = alice.id;
      post2.user = alice.id;

      unwrap(co_await post1.save(), "re-assign post1");
      unwrap(co_await post2.save(), "re-assign post2");

      // ---------- A. Call from the DEFINER side first (Post) ----------
      auto cnt = unwrap(co_await post1.addRelation<"likes", User>(alice), "post1 likes alice");
      expect(cnt == 1, "expected 1 from Post side");

      // Idempotent
      cnt = unwrap(co_await post1.addRelation<"likes", User>(alice), "post1 likes alice again");
      expect(cnt == 0, "expected 0 on duplicate");

      // ---------- B. Call from the TARGET side (User) ----------
      cnt = unwrap(co_await bob.addRelation<"likes", Post>(post1), "bob likes post1");
      expect(cnt == 1, "bob like insert");

      // ---------- C. manyRelated from both sides ----------
      auto aliceLikes = unwrap(co_await alice.manyRelated<"likes", Post>().select(), "alice likes");
      expect(aliceLikes.size() == 1, "Alice should like 1 post so far");

      auto post1Likers = unwrap(co_await post1.manyRelated<"likes", User>().select(), "post1 likers");
      expect(post1Likers.size() == 2, "post1 should have 2 likers (alice + bob)");

      // ---------- D. remove ----------
      auto removed = unwrap(co_await alice.removeRelation<"likes", Post>(post1), "remove like");
      expect(removed == 1, "expected 1 row removed");

      aliceLikes = unwrap(co_await alice.manyRelated<"likes", Post>().select(), "after remove");
      expect(aliceLikes.empty(), "Alice should have 0 likes left");
    });

    SelectQuery<Post, User> query("p");
    using Pred = Predicate<Post, User>;

    auto p = Pred::equals(&Post::user, &User::id, {"p", "u"});
    query.join<User>(p, "u");
    query.allColumns<Post>("p", "p");
    query.allColumns<User>("u", "u");
    auto queryResult = unwrap(co_await query.execute());
    SPDLOG_DEBUG(queryResult.toString());
    std::vector<std::tuple<Post, User>> result = hydrateJoined<std::tuple<Post, User>>(queryResult, "p", "u");

    for (const auto postUser : result) {
      SPDLOG_DEBUG("============================================================");
      SPDLOG_DEBUG(std::get<0>(postUser).toString(1));
      SPDLOG_DEBUG(std::get<1>(postUser).toString(1));
      SPDLOG_DEBUG("----");
    }

    // =========================================================================
    // 6. Many-to-many with extra fields on through model (PostLike.likedAt)
    // =========================================================================
    co_await runner.run("M2M with through-object upsert (extra columns)", [&]() -> Task<void> {
      PostLike<User, Post> likeRow;
      likeRow.userId = charlie.id;
      likeRow.postId = post1.id;
      // likedAt is auto-generated

      auto cnt = unwrap(co_await Post::upsertRelationThrough<"likes", User>(likeRow), "upsert through obj");
      expect(cnt >= 1, "through upsert failed");

      auto charlieLikes = unwrap(co_await charlie.manyRelated<"likes", Post>().select(), "charlie likes");
      expect(charlieLikes.size() == 1 && charlieLikes[0].id == post1.id, "charlie should like post1");
    });

    // =========================================================================
    // 7. Symmetric many-to-many  – friendship (DOUBLE_ROW)
    // =========================================================================
    co_await runner.run("symmetric M2M friendship (DOUBLE_ROW)", [&]() -> Task<void> {
      // Alice ↔ Bob
      auto cnt = unwrap(co_await alice.addRelation<"friendship", User>(bob), "friend alice-bob");
      expect(cnt == 2, "DOUBLE_ROW should insert 2 rows"); // one each direction

      // Idempotent
      cnt = unwrap(co_await alice.addRelation<"friendship", User>(bob), "friend again");
      expect(cnt == 0, "should be 0 on duplicate");

      // Alice ↔ Charlie
      cnt = unwrap(co_await alice.addRelation<"friendship", User>(charlie), "friend alice-charlie");
      expect(cnt == 2, "second friendship");

      // Query both directions
      auto aliceFriends = unwrap(co_await alice.manyRelated<"friendship", User>().select(), "alice friends");
      expect(aliceFriends.size() == 2, "Alice should have 2 friends");

      auto bobFriends = unwrap(co_await bob.manyRelated<"friendship", User>().select(), "bob friends");
      expect(bobFriends.size() == 1 && bobFriends[0].id == alice.id, "Bob should only see Alice");

      // Remove (should delete both rows)
      auto removed = unwrap(co_await alice.removeRelation<"friendship", User>(bob), "unfriend");
      expect(removed == 2, "DOUBLE_ROW remove should delete 2 rows");

      aliceFriends = unwrap(co_await alice.manyRelated<"friendship", User>().select(), "after unfriend");
      expect(aliceFriends.size() == 1, "only Charlie left");
    });

    // =========================================================================
    // 8. Asymmetric self-referential M2M – follows (via Follow)
    // =========================================================================
    co_await runner.run("asymmetric M2M follows", [&]() -> Task<void> {
      // Alice follows Bob
      auto cnt = unwrap(co_await alice.addRelation<"follows", User>(bob), "alice follows bob");
      expect(cnt == 1, "follow insert");

      // Bob follows Alice (different direction – allowed)
      cnt = unwrap(co_await bob.addRelation<"follows", User>(alice), "bob follows alice");
      expect(cnt == 1, "reverse follow");

      // Alice follows Charlie
      cnt = unwrap(co_await alice.addRelation<"follows", User>(charlie), "alice follows charlie");
      expect(cnt == 1, "second follow");

      // Query following (FORWARD)
      auto aliceFollowing = unwrap(co_await alice.manyRelated<"follows", User>().select(), "alice following");
      expect(aliceFollowing.size() == 2, "Alice should follow 2 people");

      // Query followers (REVERSE)
      auto aliceFollowers = unwrap(co_await alice.manyRelated<"followers", User>().select(), "alice followers");
      expect(aliceFollowers.size() == 1 && aliceFollowers[0].id == bob.id, "Alice should have 1 follower (Bob)");

      // Remove
      auto removed = unwrap(co_await alice.removeRelation<"follows", User>(bob), "unfollow");
      expect(removed == 1, "unfollow count");

      aliceFollowing = unwrap(co_await alice.manyRelated<"follows", User>().select(), "after unfollow");
      expect(aliceFollowing.size() == 1, "only Charlie left");
    });

    // =========================================================================
    // 9. Through model with extra data + unique constraint
    // =========================================================================
    co_await runner.run("Follow through model extra fields + uniqueness", [&]() -> Task<void> {
      Follow<User> f;
      f.follower = diana.id;
      f.followee = alice.id;
      f.randomNum = 42;
      f.str = "hello";

      auto cnt = unwrap(co_await User::upsertRelationThrough<"follows", User>(f), "upsert follow with extras");
      expect(cnt >= 1, "follow with extras failed");
    });

    // =========================================================================
    // 10. Cascade delete on through tables
    // =========================================================================
    co_await runner.run("CASCADE on through tables", [&]() -> Task<void> {
      // Destroy Charlie – his likes and follows should disappear
      unwrap(co_await charlie.destroy(), "destroy charlie");

      auto remainingLikes = unwrap(co_await PostLike<User, Post>::queryAll().count(), "likes count");
      // Only the likes that did not involve Charlie should remain
      // (exact number depends on previous steps, but Charlie’s row must be gone)

      auto charlieFollows = unwrap(co_await Follow<User>::filter(Pf::equals(&Follow<User>::follower, charlie.id) or
                                                                 Pf::equals(&Follow<User>::followee, charlie.id))
                                       .count(),
                                   "charlie follows left");
      expect(charlieFollows == 0, "Charlie’s follow rows should have been cascaded");
    });

    // =========================================================================
    // 11. Empty / missing relation edge cases
    // =========================================================================
    co_await runner.run("empty relation queries", [&]() -> Task<void> {
      User lonely = {.name = "Lonely", .email = "lonely@example.com"};
      unwrap(co_await lonely.save(), "lonely save");

      auto friends = unwrap(co_await lonely.manyRelated<"friendship", User>().select(), "lonely friends");
      expect(friends.empty(), "lonely should have 0 friends");

      auto following = unwrap(co_await lonely.manyRelated<"follows", User>().select(), "lonely following");
      expect(following.empty(), "lonely should follow 0");

      auto posts = unwrap(co_await lonely.related<Post>().select(), "lonely posts");
      expect(posts.empty(), "lonely should have 0 posts");
    });

    // =========================================================================
    // Final cleanup (optional – keeps DB tidy)
    // =========================================================================
    co_await runner.run("final cleanup", []() -> Task<void> {
      unwrap(co_await PostLike<User, Post>::bulkDestroy({true}), "final PostLike");
      unwrap(co_await Follow<User>::bulkDestroy({true}), "final Follow");
      unwrap(co_await Post::bulkDestroy({true}), "final Post");
      unwrap(co_await User::bulkDestroy({true}), "final User");
    });

    json summary = runner.toJson();
    int status = summary["allPassed"].get<bool>() ? 200 : 500;
    co_return HttpResponse(status, "application/json", summary.dump(2));
  });

  // ---------------------------------------------------------------------------
  // GET /tests/orm/predicates_test
  // ---------------------------------------------------------------------------
  router.get("/tests/orm/predicates_test", [db](const HttpRequest &request) -> Task<Response> {
    using namespace models;
    using namespace testutil;

    using Pu = Predicate<User>;

    TestRunner runner;

    // =========================================================================
    // 0. Cleanup
    // =========================================================================
    co_await runner.run("cleanup", [db]() -> Task<void> {
      unwrap(co_await User::bulkDestroy({true}), "bulkDestroy");

      auto count = unwrap(co_await User::queryAll().count(), "count");
      expect(count == 0, "expected empty users table");
    });

    // =========================================================================
    // 1. Seed predictable users
    // =========================================================================
    co_await runner.run("seed users", []() -> Task<void> {
      std::vector<User> users = {
          {.name = "Alice", .email = "alice@example.com", .age = 20, .password = "secret"},
          {.name = "Bob", .email = "bob@example.com", .age = 25, .password = "secret"},
          {.name = "Charlie", .email = "charlie@example.com", .age = 30, .password = "secret"},
          {.name = "David", .email = "david@example.com", .age = 35, .password = "secret"},
          {.name = "Diana", .email = "diana@example.com", .age = std::nullopt, .password = std::nullopt},
      };

      auto [insertedCount, insertedRows] = unwrap(co_await User::bulkInsert(users), "bulkInsert");

      expect(insertedCount == 5, "expected 5 inserted users, got " + std::to_string(insertedCount));
    });

    // =========================================================================
    // 2. EQUALS
    // =========================================================================
    co_await runner.run("equals", []() -> Task<void> {
      auto pred = Pu::equals(&User::name, std::string("Alice"));

      auto rows = unwrap(co_await User::filter(pred).select(), "equals select");

      expect(rows.size() == 1, "expected 1 row");
      expect(rows[0].name == "Alice", "expected Alice");

      expect(pred.toString() == "( a.name = ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 3. NOT_EQUALS
    // =========================================================================
    co_await runner.run("notEquals", []() -> Task<void> {
      auto pred = Pu::notEquals(&User::name, std::string("Alice"));

      auto rows = unwrap(co_await User::filter(pred).select(), "notEquals select");

      expect(rows.size() == 4, "expected 4 rows");

      expect(pred.toString() == "( a.name != ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 4. GREATER
    // =========================================================================
    co_await runner.run("greater", []() -> Task<void> {
      auto pred = Pu::greater(&User::age, 25);

      auto rows = unwrap(co_await User::filter(pred).select(), "greater select");

      expect(rows.size() == 2, "expected Charlie and David");
      expect(pred.toString() == "( a.age > ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 5. LESSER
    // =========================================================================
    co_await runner.run("lesser", []() -> Task<void> {
      auto pred = Pu::lesser(&User::age, 25);

      auto rows = unwrap(co_await User::filter(pred).select(), "lesser select");

      expect(rows.size() == 1, "expected only Alice");
      expect(rows[0].name == "Alice", "expected Alice");

      expect(pred.toString() == "( a.age < ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 6. GREATER OR EQUAL
    // =========================================================================
    co_await runner.run("greaterOrEqual", []() -> Task<void> {
      auto pred = Pu::greaterOrEqual(&User::age, 30);

      auto rows = unwrap(co_await User::filter(pred).select(), "greaterOrEqual select");

      expect(rows.size() == 2, "expected Charlie and David");
      expect(pred.toString() == "( a.age >= ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 7. LESSER OR EQUAL
    // =========================================================================
    co_await runner.run("lesserOrEqual", []() -> Task<void> {
      auto pred = Pu::lesserOrEqual(&User::age, 25);

      auto rows = unwrap(co_await User::filter(pred).select(), "lesserOrEqual select");

      expect(rows.size() == 2, "expected Alice and Bob");
      expect(pred.toString() == "( a.age <= ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 8. IS NULL
    // =========================================================================
    co_await runner.run("isNull", []() -> Task<void> {
      auto pred = Pu::isNull(&User::age);

      auto rows = unwrap(co_await User::filter(pred).select(), "isNull select");

      expect(rows.size() == 1, "expected one user with NULL age");
      expect(rows[0].name == "Diana", "expected Diana");

      expect(pred.toString() == "( a.age IS NULL )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 9. IS NOT NULL
    // =========================================================================
    co_await runner.run("isNotNull", []() -> Task<void> {
      auto pred = Pu::isNotNull(&User::age);

      auto rows = unwrap(co_await User::filter(pred).select(), "isNotNull select");

      expect(rows.size() == 4, "expected four users with non-null age");

      expect(pred.toString() == "( a.age IS NOT NULL )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 10. LIKE
    // =========================================================================
    co_await runner.run("like", []() -> Task<void> {
      auto pred = Pu::like(&User::name, std::string("A%"));

      auto rows = unwrap(co_await User::filter(pred).select(), "like select");

      expect(rows.size() == 1, "expected Alice");
      expect(rows[0].name == "Alice", "expected Alice");

      expect(pred.toString() == "( a.name LIKE ?  ESCAPE '\\' )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 11. ILIKE
    // =========================================================================
    co_await runner.run("ilike", []() -> Task<void> {
      auto pred = Pu::ilike(&User::name, std::string("alice"));

      auto rows = unwrap(co_await User::filter(pred).select(), "ilike select");

      expect(rows.size() == 1, "expected Alice");
      expect(rows[0].name == "Alice", "expected Alice");

      expect(pred.toString() == "( LOWER(a.name) LIKE LOWER(?)  ESCAPE '\\' )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 12. CONTAINS
    // =========================================================================
    co_await runner.run("contains", []() -> Task<void> {
      auto pred = Pu::contains(&User::name, std::string("li"));

      auto rows = unwrap(co_await User::filter(pred).select(), "contains select");

      expect(rows.size() == 2, "expected Alice and Charlie");

      expect(pred.toString() == "( a.name LIKE ?  ESCAPE '\\' )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 13. ICONTAINS
    // =========================================================================
    co_await runner.run("iContains", []() -> Task<void> {
      auto pred = Pu::iContains(&User::name, std::string("AL"));

      auto rows = unwrap(co_await User::filter(pred).select(), "iContains select");

      expect(rows.size() == 1, "expected Alice");
      expect(rows[0].name == "Alice", "expected Alice");

      expect(pred.toString() == "( LOWER(a.name) LIKE LOWER(?)  ESCAPE '\\' )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 14. STARTS WITH
    // =========================================================================
    co_await runner.run("startsWith", []() -> Task<void> {
      auto pred = Pu::startsWith(&User::name, std::string("A"));

      auto rows = unwrap(co_await User::filter(pred).select(), "startsWith select");

      expect(rows.size() == 1, "expected Alice");
      expect(rows[0].name == "Alice", "expected Alice");
    });

    // =========================================================================
    // 15. ENDS WITH
    // =========================================================================
    co_await runner.run("endsWith", []() -> Task<void> {
      auto pred = Pu::endsWith(&User::name, std::string("e"));

      auto rows = unwrap(co_await User::filter(pred).select(), "endsWith select");

      expect(rows.size() == 2, "expected Alice and Charlie");
    });

    // =========================================================================
    // 16. IN
    // =========================================================================
    co_await runner.run("in", []() -> Task<void> {
      auto pred = Pu::in(&User::name, std::vector<std::string>{"Alice", "Charlie", "Diana"});

      auto rows = unwrap(co_await User::filter(pred).select(), "in select");

      expect(rows.size() == 3, "expected Alice, Charlie and Diana");

      expect(pred.toString() == "( a.name IN (  ? , ? , ?  ) )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 17. NOT IN
    // =========================================================================
    co_await runner.run("notIn", []() -> Task<void> {
      auto pred = Pu::notIn(&User::name, std::vector<std::string>{"Alice", "Bob"});

      auto rows = unwrap(co_await User::filter(pred).select(), "notIn select");

      expect(rows.size() == 3, "expected Charlie, David and Diana");

      expect(pred.toString() == "( a.name NOT IN (  ? , ?  ) )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 18. BETWEEN
    // =========================================================================
    co_await runner.run("between", []() -> Task<void> {
      auto pred = Pu::between(&User::age, 25, 30);

      auto rows = unwrap(co_await User::filter(pred).select(), "between select");

      expect(rows.size() == 2, "expected Bob and Charlie");

      expect(pred.toString() == "( a.age BETWEEN ? AND ? )", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 19. AND
    // =========================================================================
    co_await runner.run("and", []() -> Task<void> {
      auto pred = Pu::greaterOrEqual(&User::age, 25) && Pu::lesserOrEqual(&User::age, 30);

      auto rows = unwrap(co_await User::filter(pred).select(), "and select");

      expect(rows.size() == 2, "expected Bob and Charlie");

      expect(pred.toString() == "(( a.age >= ? ) AND ( a.age <= ? ))", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 20. OR
    // =========================================================================
    co_await runner.run("or", []() -> Task<void> {
      auto pred = Pu::equals(&User::name, std::string("Alice")) || Pu::equals(&User::name, std::string("Diana"));

      auto rows = unwrap(co_await User::filter(pred).select(), "or select");

      expect(rows.size() == 2, "expected Alice and Diana");

      expect(pred.toString() == "(( a.name = ? ) OR ( a.name = ? ))", "unexpected SQL: " + pred.toString());
    });

    // =========================================================================
    // 21. Nested AND / OR
    //
    // (age >= 30 AND age <= 35) OR name = Diana
    // =========================================================================
    co_await runner.run("nested boolean predicates", []() -> Task<void> {
      auto ageRange = Pu::greaterOrEqual(&User::age, 30) && Pu::lesserOrEqual(&User::age, 35);

      auto pred = ageRange || Pu::equals(&User::name, std::string("Diana"));

      auto rows = unwrap(co_await User::filter(pred).select(), "nested select");

      expect(rows.size() == 3, "expected Charlie, David and Diana");
    });

    // =========================================================================
    // 22. TRUE / FALSE identity behavior
    // =========================================================================
    co_await runner.run("true false predicates", []() -> Task<void> {
      auto namePred = Pu::equals(&User::name, std::string("Alice"));

      auto trueAnd = namePred && Pu(true);
      auto falseAnd = namePred && Pu(false);

      auto trueOr = namePred || Pu(true);
      auto falseOr = namePred || Pu(false);

      auto rows = unwrap(co_await User::filter(trueAnd).select(), "true AND select");
      expect(rows.size() == 1, "TRUE AND predicate should preserve predicate");

      rows = unwrap(co_await User::filter(falseAnd).select(), "false AND select");
      expect(rows.empty(), "FALSE AND predicate should return no rows");

      rows = unwrap(co_await User::filter(trueOr).select(), "true OR select");
      expect(rows.size() == 5, "TRUE OR predicate should return all rows");

      rows = unwrap(co_await User::filter(falseOr).select(), "false OR select");
      expect(rows.size() == 1, "FALSE OR predicate should preserve predicate");
    });

    // =========================================================================
    // 23. Explicit table alias
    // =========================================================================
    co_await runner.run("explicit table alias", []() -> Task<void> {
      auto pred = Pu::equals(&User::name, std::string("Alice"), "users");
      expect(pred.toString() == "( users.name = ? )", "unexpected aliased SQL: " + pred.toString());
      co_return;
    });

    // =========================================================================
    // 24. Function predicates
    // =========================================================================
    co_await runner.run("function predicate", []() -> Task<void> {
      auto pred = Pu::iContains(&User::name, std::string("LI"));

      expect(pred.toString() == "( LOWER(a.name) LIKE LOWER(?)  ESCAPE '\\' )",
             "unexpected function SQL: " + pred.toString());

      auto rows = unwrap(co_await User::filter(pred).select(), "function select");

      expect(rows.size() == 2, "expected Alice and Charlie");
    });

    // =========================================================================
    // 25. resolvePredicates() + parameter collection
    // =========================================================================
    co_await runner.run("resolve predicates", []() -> Task<void> {
      auto pred = Pu::between(&User::age, 20, 30);

      std::vector<db::DbValue> params;
      auto sql = pred.resolvePredicates(params);

      expect(sql == "a.age BETWEEN ? AND ?", "unexpected resolved SQL: " + sql);

      expect(params.size() == 2, "expected 2 parameters, got " + std::to_string(params.size()));
      co_return;
    });

    // =========================================================================
    // 26. Predicate composition + parameter ordering
    // =========================================================================
    co_await runner.run("parameter ordering", []() -> Task<void> {
      auto pred =
          Pu::equals(&User::name, std::string("Alice")) && Pu::greater(&User::age, 18) && Pu::isNotNull(&User::email);

      std::vector<db::DbValue> params;
      auto sql = pred.resolvePredicates(params);

      expect(params.size() == 2, "expected 2 parameters, got " + std::to_string(params.size()));

      expect(sql.find("a.name = ?") != std::string::npos, "name predicate missing: " + sql);

      expect(sql.find("a.age > ?") != std::string::npos, "age predicate missing: " + sql);

      expect(sql.find("a.email IS NOT NULL") != std::string::npos, "IS NOT NULL predicate missing: " + sql);
      co_return;
    });

    // =========================================================================
    // 27. Field-to-field comparison
    //
    // This tests PredicateType::FIELD_COMPARISON directly. We don't execute it
    // because comparing arbitrary User columns isn't necessarily meaningful.
    // =========================================================================
    co_await runner.run("field comparison", []() -> Task<void> {
      auto pred = Pu::equals(&User::age, &User::id);

      expect(pred.toString() == " a.age = a.id ", "unexpected field comparison SQL: " + pred.toString());

      std::vector<db::DbValue> params;
      auto sql = pred.resolvePredicates(params);

      expect(sql == " a.age = a.id ", "unexpected resolved field comparison SQL: " + sql);

      expect(params.empty(), "field comparison should have no parameters");
      co_return;
    });

    // =========================================================================
    // 28. Complex predicate actually executed
    //
    // (age >= 20 AND age <= 30) AND name != Bob
    // => Alice + Charlie
    // =========================================================================
    co_await runner.run("complex predicate", []() -> Task<void> {
      auto ageRange = Pu::between(&User::age, 20, 30);

      auto pred = ageRange && Pu::notEquals(&User::name, std::string("Bob"));

      auto rows = unwrap(co_await User::filter(pred).select(), "complex select");

      expect(rows.size() == 2, "expected Alice and Charlie");

      for (const auto &user : rows) {
        expect(user.name == "Alice" || user.name == "Charlie", "unexpected user in result: " + *user.name);
      }
    });

    // =========================================================================
    // 29. Empty IN / NOT IN
    //
    // These are worth testing because resolvePredicates() has special handling
    // based on values.size().
    // =========================================================================
    co_await runner.run("empty IN predicates", []() -> Task<void> {
      auto pred = Pu::in(&User::name, std::vector<std::string>{});

      std::vector<db::DbValue> params;
      auto sql = pred.resolvePredicates(params);

      expect(params.empty(), "empty IN should have no parameters");

      // The current implementation treats empty values as a non-group
      // predicate and emits `name IN ?`, so this test intentionally documents
      // the current behavior. If empty IN should instead become FALSE, change
      // the implementation and this assertion.
      expect(sql.find("a.name IN") != std::string::npos, "expected IN predicate in SQL: " + sql);
      co_return;
    });

    // =========================================================================
    // 30. Final cleanup
    // =========================================================================
    co_await runner.run("final cleanup", []() -> Task<void> {
      unwrap(co_await User::bulkDestroy({true}), "bulkDestroy");

      auto count = unwrap(co_await User::queryAll().count(), "count");
      expect(count == 0, "expected empty users table after cleanup");
    });

    json summary = runner.toJson();
    int status = summary["allPassed"].get<bool>() ? 200 : 500;

    co_return HttpResponse(status, "application/json", summary.dump(2));
  });

  // ---------------------------------------------------------------------------
  // GET /tests/orm/cte_test
  // ---------------------------------------------------------------------------
  router.get("/tests/orm/cte_test", [db](const HttpRequest &request) -> Task<Response> {
    using namespace models;
    using namespace testutil;
    using Pu = Predicate<User>;

    TestRunner runner;

    // =========================================================================
    // 0. Cleanup – start from a known-empty state
    // =========================================================================
    co_await runner.run("cleanup", [db]() -> Task<void> {
      unwrap(co_await User::bulkDestroy(Pu(true)), "User bulkDestroy");

      expect(unwrap(co_await User::queryAll().count(), "count") == 0, "users not empty");
    });

    // =========================================================================
    // 1. Seed users
    // =========================================================================
    User alice, betty, bob, charlie, diana, jack, johnny;
    co_await runner.run("seed users", [&]() -> Task<void> {
      alice = {.name = "Alice", .email = "alice@example.com", .age = 55};
      betty = {.name = "Betty", .email = "betty@example.com", .age = 17};
      bob = {.name = "Bob", .email = "bob@example.com", .age = 28};
      charlie = {.name = "Charlie", .email = "charlie@example.com", .age = 25};
      diana = {.name = "Diana", .email = "diana@example.com", .age = 35};
      jack = {.name = "Jack", .email = "jack@example.com", .age = 17};
      johnny = {.name = "Johnny", .email = "johnny@example.com", .age = 1};

      unwrap(co_await alice.save(), "alice save");
      unwrap(co_await betty.save(), "betty save");
      unwrap(co_await bob.save(), "bob save");

      charlie.mother = alice;
      diana.mother = alice;

      unwrap(co_await charlie.save(), "charlie save");
      unwrap(co_await diana.save(), "diana save");

      jack.mother = diana;
      johnny.mother = betty;
      unwrap(co_await jack.save(), "jack save");
      unwrap(co_await johnny.save(), "johnny save");

      expect(alice.id > 0 and betty.id > 0 and bob.id > 0 and charlie.id > 0 and diana.id > 0 and jack.id > 0 and
                 johnny.id > 0,
             "ids not set");
    });

    // =========================================================================
    // 2. Basic CTE
    // =========================================================================
    co_await runner.run("basic CTE", [&]() -> Task<void> {
      auto adultUsers = User::filter(Pu::greaterOrEqual(&User::age, 18));

      auto mainQuery =
          User::filter(Pu::lesser(&User::age, 30)).withCte("adults", adultUsers).from("adults", "a").allColumns<User>();

      auto adultsBelow30 = unwrap(co_await mainQuery.select(), "selecting users");
      expect(adultsBelow30.size() == 2, "expected 2 adults below 30");
    });

    // =========================================================================
    // 3. CTE within CTE
    // =========================================================================
    co_await runner.run("CTE within CTE", [&]() -> Task<void> {
      auto adultUsers = User::filter((Pu::greater(&User::age, 18)));

      auto adultUsersBelow30 = User::queryAll()
                                   .withCte("adults", adultUsers)
                                   .allColumns<User>()
                                   .from("adults", "a")
                                   .where(Pu::lesser(&User::age, 30));

      auto mainQuery =
          User::queryAll().withCte("adultsBelow30", adultUsersBelow30).allColumns<User>().from("adultsBelow30", "a");

      auto adultsBelow30 = unwrap(co_await mainQuery.select(), "selecting users");
      expect(adultsBelow30.size() == 2, "expected 2 adults below 30");
    });

    charlie.mother = alice;
    diana.mother = alice;
    jack.mother = diana;
    johnny.mother = betty;

    unwrap(co_await charlie.save(), "charlie save");
    unwrap(co_await diana.save(), "diana save");
    unwrap(co_await jack.save(), "jack save");
    unwrap(co_await johnny.save(), "johnny save");

    // =========================================================================
    // 4. Join CTE
    // =========================================================================
    co_await runner.run("Join-ing with a CTE", [&]() -> Task<void> {
      auto motherStats = User::queryAll()
                             .field(&User::mother)
                             .functionField("COUNT", &User::id, "", "childCount")
                             .where(Pu::isNotNull(&User::mother))
                             .groupBy(&User::mother);

      auto mainQuery = User::queryAll("u")
                           .withCte("motherStats", motherStats)
                           .field(&User::name, "u", "name")
                           .column("childCount", "ms", "childCount")
                           .join("motherStats", Pu("u.id", Operator::EQUALS, "ms.mother"), "ms")
                           .orderBy("childCount", Sorting::DESC, "ms");

      db::QueryResult mainQueryResult = unwrap(co_await mainQuery.execute(), "selecting users");

      using childCountTuple = std::tuple<std::string, int64_t>;
      auto childCounts = hydrateTuple<childCountTuple>(mainQueryResult, std::make_tuple("name", "childCount"));

      expect(childCounts.size() == 3, "expected 3 users with children");
    });

    // =========================================================================
    // 6. Multiple CTEs
    // =========================================================================
    co_await runner.run("Multiple CTEs", [&]() -> Task<void> {
      auto adults = User::filter((Pu::greaterOrEqual(&User::age, 18)));

      auto motherStats = User::queryAll()
                             .field(&User::mother)
                             .functionField("COUNT", &User::id, "", "childCount")
                             .where(Pu::isNotNull(&User::mother))
                             .groupBy(&User::mother);

      auto mainQuery = User::queryAll()
                           .withCte("adults", adults)
                           .withCte("motherStats", motherStats)
                           .column("name", "a")
                           .column("age", "a")
                           .column("childCount", "ms")
                           .from("adults", "a")
                           .join("motherStats", Pu("a.id", Operator::EQUALS, "ms.mother"), "ms")
                           .orderBy("childCount", Sorting::DESC, "ms");

      auto queryResult = unwrap(co_await mainQuery.execute(), "selecting users");

      auto adultMothers = hydrateTuple<std::tuple<std::string, int64_t, int64_t>>(
          queryResult, std::make_tuple("name", "age", "childCount"));

      expect(adultMothers.size() == 2, "expected 2 adults with children");
    });

    jack.bestFriend = betty;
    charlie.bestFriend = bob;
    diana.bestFriend = alice;
    bob.bestFriend = diana;
    unwrap(co_await jack.save(), "jack save");
    unwrap(co_await charlie.save(), "charlie save");
    unwrap(co_await diana.save(), "diana save");
    unwrap(co_await bob.save(), "bob save");

    // =========================================================================
    // 7. CTE referenced multiple times
    // =========================================================================
    co_await runner.run("CTE referenced multiple times", [&]() -> Task<void> {
      auto adults = User::filter((Pu::greaterOrEqual(&User::age, 18)));

      auto mainQuery = User::queryAll()
                           .withCte("adults", adults)
                           .column("name", "a", "username")
                           .column("name", "b", "friendname")
                           .from("adults", "a")
                           .join("adults", Pu("a.best_friend", Operator::EQUALS, "b.id"), "b");

      auto queryResult = unwrap(co_await mainQuery.execute(), "selecting users");

      auto adultsFriendships =
          hydrateTuple<std::tuple<std::string, std::string>>(queryResult, std::make_tuple("username", "friendname"));

      expect(adultsFriendships.size() == 3, "expected 3 adults with best friends");
    });

    // =========================================================================
    // 8. CTE unused
    // =========================================================================
    co_await runner.run("CTE unused", [&]() -> Task<void> {
      auto adults = User::filter((Pu::greaterOrEqual(&User::age, 18)));

      auto mainQuery = User::queryAll().withCte("adults", adults);

      auto allUsers = unwrap(co_await mainQuery.select(), "selecting users");

      expect(allUsers.size() == 7, "expected 7 users");
    });

    // =========================================================================
    // 9. CTE name collision with tableName
    // =========================================================================
    co_await runner.run("CTE name collision with tableName", [&]() -> Task<void> {
      Cte cte{.name = "users", .sql = "SELECT 123 as id, 'fake' as name"};
      auto mainQuery = User::queryAll().withCte(cte).column("id", "u").column("name", "u").from("users", "u");
      auto queryResult = unwrap(co_await mainQuery.execute(), "selecting users");
      expect(queryResult.size() == 1, "expected 1 row");
    });

    // =========================================================================
    // 10. CTE chain
    // =========================================================================
    co_await runner.run("CTE chain", [&]() -> Task<void> {
      auto adults = User::filter((Pu::greaterOrEqual(&User::age, 18)));
      auto youngAdults = User::filter((Pu::lesser(&User::age, 30, "b"))).allColumns<User>("b").from("adults", "b");
      auto namedAdults = User::filter((Pu::isNotNull(&User::name, "y"))).allColumns<User>("y").from("youngAdults", "y");

      auto mainQuery = User::queryAll()
                           .withCte("adults", adults)
                           .withCte("youngAdults", youngAdults)
                           .withCte("namedAdults", namedAdults)
                           .column("name", "a")
                           .from("namedAdults", "a");

      auto queryResult = unwrap(co_await mainQuery.execute(), "selecting users");
      expect(queryResult.size() == 2, "expected 2 users");
    });

    // =========================================================================
    // 11. Recursive CTE
    // =========================================================================
    co_await runner.run("Recursive CTE - Ancestors", [&]() -> Task<void> {
      auto query = [](User user) {
        auto base = User::filter(Pu::equals(&User::id, user));
        auto recursive = User::queryAll().join("ancestors", Pu::equals(&User::id, &User::mother, {"a", "x"}), "x");

        auto mainQuery = SelectQuery<User>()
                             .withCte("ancestors", base.unionQuery(recursive), Cte::Type::RECURSIVE)
                             .allColumns<User>()
                             .from("ancestors", "a");
        return mainQuery;
      };

      auto jacksAncestors = unwrap(co_await query(jack).select(), "selecting jack's ancestors");
      expect(jacksAncestors.size() == 3, "expected 3 ancestors");

      auto johnnysAncestors = unwrap(co_await query(johnny).select(), "selecting johnny's ancestors");
      expect(johnnysAncestors.size() == 2, "expected 2 ancestors");
    });

    // =========================================================================
    // Final cleanup (optional – keeps DB tidy)
    // =========================================================================
    co_await runner.run("final cleanup",
                        []() -> Task<void> { unwrap(co_await User::bulkDestroy({true}), "final User"); });

    json summary = runner.toJson();
    int status = summary["allPassed"].get<bool>() ? 200 : 500;
    co_return HttpResponse(status, "application/json", summary.dump(2));
  });
}
