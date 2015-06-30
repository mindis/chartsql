/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <chartsql/runtime/ExecutionContext.h>

using namespace fnord;

namespace csql {

ExecutionContext::ExecutionContext() : cancelled_(false) {}

void ExecutionContext::onStatusChange(
    Function<void (const ExecutionStatus& status)> fn) {
  std::unique_lock<std::mutex> lk(mutex_);
  on_status_change_ = fn;
}

void ExecutionContext::onCancel(Function<void ()> fn) {
  std::unique_lock<std::mutex> lk(mutex_);
  on_cancel_ = fn;
}

void ExecutionContext::cancel() {
  std::unique_lock<std::mutex> lk(mutex_);

  if (on_cancel_) {
    on_cancel_();
  }

  cancelled_ = true;
}

bool ExecutionContext::isCancelled() const {
  return cancelled_;
}

void ExecutionContext::incrNumSubtasksTotal(size_t n) {
  status_.num_subtasks_total += n;
  statusChanged();
}

void ExecutionContext::incrNumSubtasksCompleted(size_t n) {
  status_.num_subtasks_completed += n;
  statusChanged();
}

void ExecutionContext::incrNumRowsScanned(size_t n) {
  status_.num_rows_scanned += n;
  statusChanged();
}

void ExecutionContext::statusChanged() {
  // FIXPAUL rate limit...

  std::unique_lock<std::mutex> lk(mutex_);
  if (on_status_change_) {
    on_status_change_(status_);
  }
}

ExecutionStatus::ExecutionStatus() :
    num_subtasks_total(1),
    num_subtasks_completed(0) {}

String ExecutionStatus::toString() const {
  return StringUtil::format(
      "$0/$1 ($2%)",
      num_subtasks_completed.load(),
      num_subtasks_total.load(),
      progress() * 100);
}

double ExecutionStatus::progress() const {
  return num_subtasks_completed / (double) num_subtasks_total;
}

}
