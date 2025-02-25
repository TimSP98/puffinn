#pragma once

#include "catch.hpp"
#include "puffinn/collection.hpp"
#include "puffinn/hash/simhash.hpp"
#include "puffinn/hash/crosspolytope.hpp"
#include "puffinn/hash_source/pool.hpp"
#include "puffinn/hash_source/independent.hpp"
#include "puffinn/hash_source/tensor.hpp"
#include "puffinn/similarity_measure/cosine.hpp"
#include "puffinn/similarity_measure/jaccard.hpp"

#include <sstream>
#include <iostream>

namespace collection {
    using namespace puffinn;

    const unsigned int MB = 1024*1024;

    TEST_CASE("get unit vector") {
        Index<CosineSimilarity> index(2, 1*MB, false);
        index.insert(std::vector<float>({1, 0}));
        index.insert(std::vector<float>({0, 1}));
        REQUIRE(index.get<std::vector<float>>(1)[0] == 0.0);
        REQUIRE(index.get<std::vector<float>>(1)[1] == Approx(1.0).margin(1.5/(1 << 15)));
    }

    TEST_CASE("get set") {
        Index<JaccardSimilarity> index(5, 1*MB, false);
        index.insert(std::vector<uint32_t>{1, 0, 4});
        index.insert(std::vector<uint32_t>{2, 3});
        REQUIRE(index.get<std::vector<uint32_t>>(0) == std::vector<uint32_t>{0, 1, 4});
        REQUIRE(index.get<std::vector<uint32_t>>(1) == std::vector<uint32_t>{2, 3});
    }

    TEST_CASE("Index::search_bf") {
        const unsigned DIMENSIONS = 2;

        std::vector<std::vector<float>> inserted {
            std::vector<float>({1, 0}),
            std::vector<float>({-1, -1}),
            std::vector<float>({1, 0.15}),
            std::vector<float>({1, 0.2}),
            std::vector<float>({1, -0.1}),
        };

        Index<CosineSimilarity> table(DIMENSIONS, 1*MB, false);
        for (auto &vec : inserted) {
            table.insert(vec);
        }
        // No rebuilding necessary

        std::vector<float> query({1, 0});

        SECTION("k = 0") {
            REQUIRE(table.search_bf(query, 0).size() == 0);
        }

        SECTION("k = 1") {
            auto res = table.search_bf(query, 1);
            REQUIRE(res.size() == 1);
            REQUIRE(res[0] == 0);
        }

        SECTION("k = 2") {
            auto res = table.search_bf(query, 2);
            REQUIRE(res.size() == 2);
            REQUIRE(res[0] == 0);
            REQUIRE(res[1] == 4);
        }

        SECTION("k = 5") {
            auto res = table.search_bf(query, 5);
            REQUIRE(res.size() == 5);
            REQUIRE(res[0] == 0);
            REQUIRE(res[1] == 4);
            REQUIRE(res[2] == 2);
            REQUIRE(res[3] == 3);
            REQUIRE(res[4] == 1);
        }

        SECTION("k > size") {
            REQUIRE(table.search_bf(query, 10).size() == 5);
        }
    }

    void test_angular_search_pq()
    {
        // This test needs limit (for pq to add to maxbuffer) to be less than 0, which is a bit odd 
        // Should we even handle edge cases where there are fewer than 256 entries
        int n = 500;
        int dimensions = 100;
        std::unique_ptr<HashSourceArgs<SimHash>> hash_source;

        const int NUM_SAMPLES = 100;

        std::vector<float> recalls = {0.2, 0.5, 0.95};
        std::vector<unsigned int> ks = {1, 10};

        std::vector<std::vector<float>> inserted;
        for (int i=0; i<n; i++) {
            inserted.push_back(UnitVectorFormat::generate_random(dimensions));
        }

        Index<CosineSimilarity, SimHash, SimHash> table(dimensions, 100*MB, true);
        if (hash_source) {
            table = Index<CosineSimilarity, SimHash, SimHash>(dimensions, 100*MB, true, *hash_source);
        }
        for (auto &vec : inserted) {
            table.insert(vec);
        }
        table.rebuild();

        for (auto k : ks) {
            for (auto recall : recalls) {
                int num_correct = 0;
                auto adjusted_k = std::min(k, table.get_size());
                
                float expected_correct = recall*adjusted_k*NUM_SAMPLES;
                for (int sample=0; sample < NUM_SAMPLES; sample++) {
                    auto query = UnitVectorFormat::generate_random(dimensions);
                    auto exact = table.search_bf(query, k);
                    auto res = table.search(query, k, recall, FilterType::PQ_Simple);

                    REQUIRE(res.size() == static_cast<size_t>(adjusted_k));
                    for (auto i : exact) {
                        // Each expected value is returned once.
                        if (std::count(res.begin(), res.end(), i) != 0) {
                            num_correct++;
                        }
                    }
                }
                // Only fail if the recall is far away from the expectation.
                REQUIRE(num_correct >= 0.8 * expected_correct);
            }
        }

    }

    template <typename T, typename U>
    void test_angular_search(
        int n,
        int dimensions,
        std::unique_ptr<HashSourceArgs<T>> hash_source = std::unique_ptr<HashSourceArgs<T>>()
    ) {
        const int NUM_SAMPLES = 100;

        std::vector<float> recalls = {0.2, 0.5, 0.95};
        std::vector<unsigned int> ks = {1, 10};

        std::vector<std::vector<float>> inserted;
        for (int i=0; i<n; i++) {
            inserted.push_back(UnitVectorFormat::generate_random(dimensions));
        }

        Index<CosineSimilarity, T, U> table(dimensions, 100*MB, false);
        if (hash_source) {
            table = Index<CosineSimilarity, T, U>(dimensions, 100*MB, false, *hash_source);
        }
        for (auto &vec : inserted) {
            table.insert(vec);
        }
        table.rebuild();

        for (auto k : ks) {
            for (auto recall : recalls) {
                int num_correct = 0;
                auto adjusted_k = std::min(k, table.get_size());
                
                float expected_correct = recall*adjusted_k*NUM_SAMPLES;
                for (int sample=0; sample < NUM_SAMPLES; sample++) {
                    auto query = UnitVectorFormat::generate_random(dimensions);
                    auto exact = table.search_bf(query, k);
                    auto res = table.search(query, k, recall);

                    REQUIRE(res.size() == static_cast<size_t>(adjusted_k));
                    for (auto i : exact) {
                        // Each expected value is returned once.
                        if (std::count(res.begin(), res.end(), i) != 0) {
                            num_correct++;
                        }
                    }
                }
                // Only fail if the recall is far away from the expectation.
                REQUIRE(num_correct >= 0.8 * expected_correct);
            }
        }
    }

    TEST_CASE("Index::search - empty") {
        test_angular_search<SimHash, SimHash>(0, 2);
    }

    TEST_CASE("Index::search pq") {
        test_angular_search_pq();
    }

    TEST_CASE("Index::search - 1 value") {
        test_angular_search<SimHash, SimHash>(1, 5);
    }

    TEST_CASE("Index::search simhash") {
        std::vector<int> dimensions = {5, 100};

        for (auto d : dimensions) {
            std::unique_ptr<HashSourceArgs<SimHash>> args =
                std::make_unique<HashPoolArgs<SimHash>>(3000);
            test_angular_search<SimHash, SimHash>(500, d, std::move(args));

            args = std::make_unique<IndependentHashArgs<SimHash>>();
            test_angular_search<SimHash, SimHash>(500, d, std::move(args));

            args = std::make_unique<TensoredHashArgs<SimHash>>();
            test_angular_search<SimHash, SimHash>(500, d, std::move(args));
        }
    }

    TEST_CASE("Index::search fht cross-polytope") {
        std::vector<int> dimensions = {5, 100};

        for (auto d : dimensions) {
            std::unique_ptr<HashSourceArgs<FHTCrossPolytopeHash>> args =
                std::make_unique<HashPoolArgs<FHTCrossPolytopeHash>>(3000);
            test_angular_search<FHTCrossPolytopeHash, SimHash>(500, d, std::move(args));

            args = std::make_unique<IndependentHashArgs<FHTCrossPolytopeHash>>();
            test_angular_search<FHTCrossPolytopeHash, SimHash>(500, d, std::move(args));


            args = std::make_unique<TensoredHashArgs<FHTCrossPolytopeHash>>();
            test_angular_search<FHTCrossPolytopeHash, SimHash>(500, d, std::move(args));
        }
    }

    void test_jaccard_search(
        int n,
        int dimensions,
        std::unique_ptr<HashSourceArgs<MinHash>> hash_source =
            std::unique_ptr<HashSourceArgs<MinHash>>()
    ) {
        const int NUM_SAMPLES = 500;

        std::vector<float> recalls = {0.2, 0.5, 0.95};
        std::vector<unsigned int> ks = {1, 10};

        std::vector<std::vector<uint32_t>> inserted;
        for (int i=0; i<n; i++) {
            inserted.push_back(SetFormat::generate_random(dimensions));
        }

        Index<JaccardSimilarity> table(dimensions, 100*MB, false);
        if (hash_source) {
            table = Index<JaccardSimilarity>(dimensions, 100*MB, false, *hash_source);
        }
        for (auto &vec : inserted) {
            table.insert(vec);
        }
        table.rebuild();

        for (auto k : ks) {
            for (auto recall : recalls) {
                int num_correct = 0;
                auto adjusted_k = std::min(k, table.get_size());
                float expected_correct = recall*adjusted_k*NUM_SAMPLES;
                for (int sample=0; sample < NUM_SAMPLES; sample++) {
                    auto query = SetFormat::generate_random(dimensions);
                    auto exact = table.search_bf(query, k);
                    auto res = table.search(query, k, recall, FilterType::None);

                    REQUIRE(res.size() == static_cast<size_t>(adjusted_k));
                    for (auto i : exact) {
                        // Each expected value is returned once.
                        if (std::count(res.begin(), res.end(), i) != 0) {
                            num_correct++;
                        }
                    }
                }
                // Only fail if the recall is far away from the expectation.
                REQUIRE(num_correct >= 0.8*expected_correct);
            }
        }
    }

    TEST_CASE("Index::search minhash") {
        std::vector<int> dimensions = {100};

        for (auto d : dimensions) {
            std::unique_ptr<HashSourceArgs<MinHash>> args =
                std::make_unique<HashPoolArgs<MinHash>>(3000);
            test_jaccard_search(500, d, std::move(args));

            args = std::make_unique<IndependentHashArgs<MinHash>>();
            test_jaccard_search(500, d, std::move(args));


            args = std::make_unique<TensoredHashArgs<MinHash>>();
            test_jaccard_search(500, d, std::move(args));
        }
    }

    TEST_CASE("Insert unit vector of wrong dimensionality") {
        Index<CosineSimilarity> index(2, 1*1024*1024, false);
        REQUIRE_THROWS(index.insert(std::vector<float>{1}));
        REQUIRE_NOTHROW(index.insert(std::vector<float>{1, 0}));
        REQUIRE_THROWS(index.insert(std::vector<float>{0, 1, 0}));
    }

    TEST_CASE("Insert set containing token outside range") {
        Index<JaccardSimilarity> index(5, 1*1024*1024, false);
        REQUIRE_NOTHROW(index.insert(std::vector<unsigned int>{}));
        REQUIRE_NOTHROW(index.insert(std::vector<unsigned int>{0, 4}));
        REQUIRE_THROWS(index.insert(std::vector<unsigned int>{5}));
    }

    TEST_CASE("Rebuild") {
        int dims = 100;
        int n = 5000;
        float recall = 0.8;
        int k = 10;
        int samples = 100;

        Index<CosineSimilarity> index(dims, 512*MB, false);
        for (int rebuilds = 0; rebuilds < 3; rebuilds++) {
            for (int i=0; i < n; i++) {
                index.insert(UnitVectorFormat::generate_random(dims));
            }
            index.rebuild();

            int num_correct = 0;
            float expected_correct = recall*k*samples;
            for (int sample=0; sample < samples; sample++) {
                auto query = UnitVectorFormat::generate_random(dims);
                auto exact = index.search_bf(query, k);
                auto res = index.search(query, k, recall);

                REQUIRE(res.size() == k);
                for (auto i : exact) {
                    if (std::count(res.begin(), res.end(), i) != 0) {
                        num_correct++;
                    }
                }
            }
            // Only fail if the recall is far away from the expectation.
            REQUIRE(num_correct >= 0.8*expected_correct);
        }
    }

    template <typename T, typename H, typename S>
    void test_serialize(
        typename T::Format::Args args,
        const HashSourceArgs<H>& hash_args,
        const HashSourceArgs<S>& sketch_args
    ) {
        int k = 50;

        Index<T, H, S> index(args, 50*MB, false, hash_args, sketch_args);
        for (int i=0; i < 1000; i++) {
            index.insert(T::Format::generate_random(args));
        }
        index.rebuild();

        auto query = T::Format::generate_random(args);
        auto res1 = index.search(query, k, 0.5);

        std::stringstream s;
        index.serialize(s);
        Index<T, H, S> deserialized(s);

        // Test that searches give the same result.
        auto res2 = deserialized.search(query, k, 0.5);
        REQUIRE(res1 == res2);

        // Test that ser(de(ser(Index))) == ser(Index), which should catch errors in parts
        // that don't show up when searching.
        std::stringstream s2;
        deserialized.serialize(s2);
        REQUIRE(s2.str() == s.str());
    }

    TEST_CASE("Serialize") {
        test_serialize<CosineSimilarity>(
            100,
            IndependentHashArgs<FHTCrossPolytopeHash>(),
            IndependentHashArgs<SimHash>());
        test_serialize<CosineSimilarity>(
            100,
            HashPoolArgs<CrossPolytopeHash>(3000),
            HashPoolArgs<SimHash>(1000));
        test_serialize<JaccardSimilarity>(
            1000,
            TensoredHashArgs<MinHash>(),
            TensoredHashArgs<MinHash1Bit>());
    }

    TEST_CASE("Serialize chunked") {
        int dims = 100;
        Index<CosineSimilarity> index(dims, 50*MB, false);
        for (int i=0; i < 1000; i++) {
            index.insert(UnitVectorFormat::generate_random(dims));
        }
        index.rebuild();

        std::stringstream s;
        index.serialize(s, true);
        auto iter = index.serialize_chunks();
        size_t len = 0;
        while (iter.has_next()) {
            iter.serialize_next(s);
            len++;
        }

        Index<CosineSimilarity> deserialized(s);
        for (size_t i=0; i < len; i++) {
            deserialized.deserialize_chunk(s);
        }

        // Test that the original and deserialized deserialize to the same string.
        std::stringstream s1, s2;
        index.serialize(s1);
        deserialized.serialize(s2);
        REQUIRE(s1.str() == s2.str());
    }

    TEST_CASE("Serialize no rebuild") {
        int dims = 100;
        Index<CosineSimilarity> index(dims, 50*MB, false);
        for (int i=0; i < 1000; i++) {
            index.insert(UnitVectorFormat::generate_random(dims));
        }
        std::stringstream s1;
        index.serialize(s1);

        Index<CosineSimilarity> deserialized(s1);
        std::stringstream s2;
        deserialized.serialize(s2);
        REQUIRE(s1.str() == s2.str());
    }

    TEST_CASE("search_from_index == search") {
        int dims = 100;
        int n = 5000;
        int k = 10;
        float recall = 0.7;

        Index<CosineSimilarity> index(dims, 100*MB, false);
        for (int i=0; i < 5000; i++) {
            index.insert(UnitVectorFormat::generate_random(dims));
        }
        auto query = UnitVectorFormat::generate_random(dims);
        index.insert(query);
        index.rebuild();

        auto res1 = index.search(query, k, recall);
        res1.erase(res1.begin());
        auto res2 = index.search_from_index(n, k, recall);
        res2.pop_back();
        REQUIRE(res1 == res2);
    }
}
