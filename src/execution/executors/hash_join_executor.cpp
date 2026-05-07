#include "onebase/execution/executors/hash_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                    std::unique_ptr<AbstractExecutor> left_executor,
                                    std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void HashJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  
  hash_table_.clear();
  result_tuples_.clear();
  cursor_ = 0;
  
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();
  
  Tuple left_tuple;
  RID left_rid;
  while (left_executor_->Next(&left_tuple, &left_rid)) {
    auto key = plan_->GetLeftKeyExpression()->Evaluate(&left_tuple, &left_schema);
    hash_table_[key.ToString()].push_back(left_tuple);
  }
  
  Tuple right_tuple;
  RID right_rid;
  while (right_executor_->Next(&right_tuple, &right_rid)) {
    auto key = plan_->GetRightKeyExpression()->Evaluate(&right_tuple, &right_schema);
    auto it = hash_table_.find(key.ToString());
    if (it != hash_table_.end()) {
      for (const auto &left_tuple : it->second) {
        std::vector<Value> combined_values;
        for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
          combined_values.push_back(left_tuple.GetValue(&left_schema, i));
        }
        for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
          combined_values.push_back(right_tuple.GetValue(&right_schema, i));
        }
        result_tuples_.emplace_back(std::move(combined_values));
      }
    }
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  return true;
}

}  // namespace onebase
