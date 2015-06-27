/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <chartsql/CSTableScan.h>
#include <chartsql/qtree/ColumnReferenceNode.h>
#include <chartsql/runtime/defaultruntime.h>
#include <chartsql/runtime/compile.h>
#include <fnord/inspect.h>

using namespace fnord;

namespace csql {

CSTableScan::CSTableScan(
    RefPtr<SelectProjectAggregateNode> stmt,
    cstable::CSTableReader&& cstable) :
    cstable_(std::move(cstable)),
    colindex_(0),
    expand_(stmt->expandNestedRecords()) {

  Set<String> column_names;
  for (const auto& expr : stmt->selectList()) {
    findColumns(expr, &column_names);
  }

  for (const auto& col : column_names) {
    columns_.emplace(col, ColumnRef(cstable_.getColumnReader(col), colindex_++));
  }

  for (auto& expr : stmt->selectList()) {
    resolveColumns(expr);
  }

  //fnord::iputs("all cols: $0", column_names);

  //for (const auto& expr : stmt->selectList()) {
  //  select_list_.emplace_back(
  //      findMaxRepetitionLevel(expr),
  //      runtime.compiler()->compile(expr),
  //      &scratch_);
  //}
}

void CSTableScan::execute(Function<bool (int argc, const SValue* argv)> fn) {
  uint64_t select_level = 0;
  uint64_t fetch_level = 0;

  Vector<SValue> in_row(colindex_, SValue{});
  Vector<SValue> out_row(select_list_.size(), SValue{});

  size_t num_records = 0;
  size_t total_records = cstable_.numRecords();
  while (num_records < total_records) {
    uint64_t next_level = 0;

    if (fetch_level == 0) {
      ++num_records;
    }

    for (auto& col : columns_) {
      auto nextr = col.second.reader->nextRepetitionLevel();

      if (nextr >= fetch_level) {
        auto& reader = col.second.reader;

        uint64_t r;
        uint64_t d;
        void* data;
        size_t size;
        reader->next(&r, &d, &data, &size);

        switch (col.second.reader->type()) {

          case msg::FieldType::STRING:
            in_row[col.second.index] =
                SValue(SValue::StringType((char*) data, size));
            break;

          case msg::FieldType::UINT32:
          case msg::FieldType::UINT64:
            switch (size) {
              case sizeof(uint32_t):
                in_row[col.second.index] =
                    SValue(SValue::IntegerType(*((uint32_t*) data)));
                break;
              case sizeof(uint64_t):
                in_row[col.second.index] =
                    SValue(SValue::IntegerType(*((uint64_t*) data)));
                break;
              case 0:
                in_row[col.second.index] = SValue();
                break;
            }
            break;

          case msg::FieldType::BOOLEAN:
            switch (size) {
              case sizeof(uint32_t):
                in_row[col.second.index] =
                    SValue(SValue::BoolType(*((uint32_t*) data) > 0));
                break;
              case sizeof(uint64_t):
                in_row[col.second.index] =
                    SValue(SValue::BoolType(*((uint64_t*) data) > 0));
                break;
              case 0:
                in_row[col.second.index] = SValue(SValue::BoolType(false));
                break;
            }
            break;

          case msg::FieldType::OBJECT:
            RAISE(kIllegalStateError);

        }
      }

      next_level = std::max(next_level, col.second.reader->nextRepetitionLevel());
    }

    fetch_level = next_level;

    if (true) { // where clause
      for (int i = 0; i < select_list_.size(); ++i) {
        if (select_list_[i].rep_level >= select_level) {
          select_list_[i].compiled->accumulate(
              &select_list_[i].instance,
              in_row.size(),
              in_row.data());
        }
      }

      if (expand_ || next_level == 0) {
        for (int i = 0; i < select_list_.size(); ++i) {
          select_list_[i].compiled->evaluate(
              &select_list_[i].instance,
              in_row.size(),
              in_row.data(),
              &out_row[i]);

          select_list_[i].compiled->reset(&select_list_[i].instance);
        }

        if (!fn(out_row.size(), out_row.data())) {
          return;
        }
      }


      select_level = fetch_level;
    } else {
      select_level = std::min(select_level, fetch_level);
    }

    for (const auto& col : columns_) {
      if (col.second.reader->maxRepetitionLevel() >= select_level) {
        in_row[col.second.index] = SValue();
      }
    }
  }
}

void CSTableScan::findColumns(
    RefPtr<ScalarExpressionNode> expr,
    Set<String>* column_names) const {
  auto fieldref = dynamic_cast<ColumnReferenceNode*>(expr.get());
  if (fieldref != nullptr) {
    column_names->emplace(fieldref->fieldName());
  }

  for (const auto& e : expr->arguments()) {
    findColumns(e, column_names);
  }
}

void CSTableScan::resolveColumns(RefPtr<ScalarExpressionNode> expr) const {
  auto fieldref = dynamic_cast<ColumnReferenceNode*>(expr.get());
  if (fieldref != nullptr) {
    auto col = columns_.find(fieldref->fieldName());
    if (col == columns_.end()) {
      RAISE(kIllegalStateError);
    }

    fieldref->setColumnIndex(col->second.index);
  }

  for (const auto& e : expr->arguments()) {
    resolveColumns(e);
  }
}

uint64_t CSTableScan::findMaxRepetitionLevel(
    RefPtr<ScalarExpressionNode> expr) const {
  uint64_t max_level = 0;

  auto fieldref = dynamic_cast<ColumnReferenceNode*>(expr.get());
  if (fieldref != nullptr) {
    auto col = columns_.find(fieldref->fieldName());
    if (col == columns_.end()) {
      RAISE(kIllegalStateError);
    }

    auto col_level = col->second.reader->maxRepetitionLevel();
    if (col_level > max_level) {
      max_level = col_level;
    }
  }

  for (const auto& e : expr->arguments()) {
    auto e_level = findMaxRepetitionLevel(e);
    if (e_level > max_level) {
      max_level = e_level;
    }
  }

  return max_level;
}

CSTableScan::ColumnRef::ColumnRef(
    RefPtr<cstable::ColumnReader> r,
    size_t i) :
    reader(r),
    index(i) {}

CSTableScan::ExpressionRef::ExpressionRef(
    size_t _rep_level,
    ScopedPtr<ScalarExpression> _compiled,
    ScratchMemory* smem) :
    rep_level(_rep_level),
    compiled(std::move(_compiled)),
    instance(compiled->allocInstance(smem)) {}

CSTableScan::ExpressionRef::ExpressionRef(
    ExpressionRef&& other) :
    rep_level(other.rep_level),
    compiled(std::move(other.compiled)),
    instance(other.instance) {
  other.instance.scratch = nullptr;
}

CSTableScan::ExpressionRef::~ExpressionRef() {
  if (instance.scratch) {
    compiled->freeInstance(&instance);
  }
}

} // namespace csql
