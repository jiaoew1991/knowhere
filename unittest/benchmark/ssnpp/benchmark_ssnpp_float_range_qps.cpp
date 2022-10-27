// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include <gtest/gtest.h>
#include <vector>

#include "benchmark_knowhere.h"
#include "unittest/range_utils.h"

class Benchmark_ssnpp_float_range_qps : public Benchmark_knowhere, public ::testing::Test {
 public:
    void
    test_ivf(const knowhere::Config& cfg) {
        auto conf = cfg;
        auto nlist = knowhere::GetIndexParamNlist(conf);

        knowhere::SetIndexParamNprobe(conf, nprobe_);
        knowhere::SetMetaTopk(conf, topk_);

        printf("\n[%0.3f s] %s | %s | nlist=%ld\n", get_time_diff(), ann_test_name_.c_str(), index_type_.c_str(),
               nlist);
        printf("           nprobe=%d, k=%d\n", nprobe_, topk_);
        printf("================================================================================\n");
        for (auto thread_num : THREAD_NUMs_) {
            CALC_TIME_SPAN(task(conf, nq_, thread_num));
            printf("  thread_num = %d, elapse = %6.3fs, QPS = %.3f\n", thread_num, t_diff, nq_ / t_diff);
            std::fflush(stdout);
        }
        printf("================================================================================\n");
        printf("[%.3f s] Test '%s/%s' done\n\n", get_time_diff(), ann_test_name_.c_str(),
               std::string(index_type_).c_str());
    }

    void
    test_hnsw(const knowhere::Config& cfg) {
        auto conf = cfg;
        auto M = knowhere::GetIndexParamHNSWM(conf);
        auto efConstruction = knowhere::GetIndexParamEfConstruction(conf);
        knowhere::SetIndexParamEf(conf, ef_);
        knowhere::SetMetaTopk(conf, topk_);

        printf("\n[%0.3f s] %s | %s | M=%ld | efConstruction=%ld\n", get_time_diff(), ann_test_name_.c_str(),
               index_type_.c_str(), M, efConstruction);
        printf("           ef=%d, k=%d\n", ef_, topk_);
        printf("================================================================================\n");
        for (auto thread_num : THREAD_NUMs_) {
            CALC_TIME_SPAN(task(conf, nq_, thread_num));
            printf("  thread_num = %d, elapse = %6.3fs, QPS = %.3f\n", thread_num, t_diff, nq_ / t_diff);
            std::fflush(stdout);
        }
        printf("================================================================================\n");
        printf("[%.3f s] Test '%s/%s' done\n\n", get_time_diff(), ann_test_name_.c_str(),
               std::string(index_type_).c_str());
    }

 private:
    void
    task(const knowhere::Config& conf, int32_t task_num, int32_t worker_num) {
        auto worker = [&](int32_t idx_start, int32_t num) {
            for (int32_t i = 0; i < num; i++) {
                knowhere::Config config = conf;
                knowhere::SetMetaRadius(config, std::sqrt(gt_radius_[idx_start + i]));
                knowhere::DatasetPtr ds_ptr =
                    knowhere::GenDataset(1, dim_, (const float*)xq_ + ((idx_start + i) * dim_));
                index_->QueryByRange(ds_ptr, config, nullptr);
            }
        };

        std::vector<std::thread> thread_vector(worker_num);
        for (int32_t i = 0; i < worker_num; i++) {
            int32_t idx_start, req_num;
            if (task_num % worker_num == 0) {
                idx_start = task_num / worker_num * i;
                req_num = task_num / worker_num;
            } else {
                idx_start = (task_num / worker_num + 1) * i;
                req_num = (i != worker_num - 1) ? (task_num / worker_num + 1)
                                                : (task_num - (task_num / worker_num + 1) * (worker_num - 1));
            }
            thread_vector[i] = std::thread(worker, idx_start, req_num);
        }
        for (int32_t i = 0; i < worker_num; i++) {
            thread_vector[i].join();
        }
    }

 protected:
    void
    SetUp() override {
        T0_ = elapsed();
        set_ann_test_name("ssnpp-256-euclidean-range");

        // load base dataset
        load_bin<float>("FB_ssnpp_database.10M.fbin", xb_, nb_, dim_);

        // load query dataset
        int32_t dim;
        load_bin<float>("FB_ssnpp_public_queries.fbin", xq_, nq_, dim);
        assert(dim_ == dim);

        int32_t gt_num;
        //std::vector<std::string> gt_array = {"SSNPP-radius-exponential", "SSNPP-gt-exponential"};
        std::vector<std::string> gt_array = {"SSNPP-radius-norm", "SSNPP-gt-norm"};
        //std::vector<std::string> gt_array = {"SSNPP-radius-poisson", "SSNPP-gt-poisson"};
        //std::vector<std::string> gt_array = {"SSNPP-radius-uniform", "SSNPP-gt-uniform"};
        // load ground truth radius
        load_range_radius(gt_array[0], gt_radius_, gt_num);
        assert(gt_num == nq_);

        // load ground truth ids
        load_range_truthset(gt_array[1], gt_ids_, gt_lims_, gt_num);
        assert(gt_num == nq_);

        metric_type_ = knowhere::metric::L2;
        nq_ = 1000;

        knowhere::SetMetaMetricType(cfg_, metric_type_);
        knowhere::KnowhereConfig::SetSimdType(knowhere::KnowhereConfig::SimdType::AVX2);
        printf("faiss::distance_compute_blas_threshold: %ld\n", knowhere::KnowhereConfig::GetBlasThreshold());
    }

    void
    TearDown() override {
        free_all();
    }

 protected:
    const int32_t topk_ = 100;
    const std::vector<int32_t> THREAD_NUMs_ = {10, 20, 30, 40, 50};

    // IVF index params
    const int32_t NLIST_ = 1024;
    const int32_t nprobe_ = 512; // ivf_flat recall: 0.8626

    // HNSW index params
    const int32_t M_ = 16;
    const int32_t EFCON_ = 200;
    const int32_t ef_ = 512;     // recall 0.1396
};

TEST_F(Benchmark_ssnpp_float_range_qps, TEST_IVF_FLAT_NM) {
    index_type_ = knowhere::IndexEnum::INDEX_FAISS_IVFFLAT;

    knowhere::Config conf = cfg_;
    knowhere::SetIndexParamNlist(conf, NLIST_);
    std::string index_file_name = get_index_name({NLIST_});

    create_index(index_file_name, conf);

    // IVFFLAT_NM should load raw data
    knowhere::BinaryPtr bin = std::make_shared<knowhere::Binary>();
    bin->data = std::shared_ptr<uint8_t[]>((uint8_t*)xb_, [&](uint8_t*) {});
    bin->size = (size_t)dim_ * nb_ * sizeof(float);
    binary_set_.Append(RAW_DATA, bin);

    index_->Load(binary_set_);
    binary_set_.clear();
    test_ivf(conf);
}

TEST_F(Benchmark_ssnpp_float_range_qps, TEST_IVF_SQ8) {
    index_type_ = knowhere::IndexEnum::INDEX_FAISS_IVFSQ8;

    knowhere::Config conf = cfg_;
    knowhere::SetIndexParamNlist(conf, NLIST_);
    std::string index_file_name = get_index_name({NLIST_});

    create_index(index_file_name, conf);
    index_->Load(binary_set_);
    binary_set_.clear();
    test_ivf(conf);
}

TEST_F(Benchmark_ssnpp_float_range_qps, TEST_HNSW) {
    index_type_ = knowhere::IndexEnum::INDEX_HNSW;

    knowhere::Config conf = cfg_;
    knowhere::SetIndexParamHNSWM(conf, M_);
    knowhere::SetIndexParamEfConstruction(conf, EFCON_);
    std::string index_file_name = get_index_name({M_, EFCON_});

    create_index(index_file_name, conf);
    index_->Load(binary_set_);
    binary_set_.clear();
    test_hnsw(conf);
}
