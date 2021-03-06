/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <stx/stdtypes.h>
#include <stx/autoref.h>
#include <chartsql/SFunction.h>

namespace csql {
class SymbolTableEntry;
class SValue;

class SymbolTableEntry {
public:

  SymbolTableEntry(
      const std::string& symbol,
      void (*method)(void*, int, SValue*, SValue*));

  SymbolTableEntry(
      const std::string& symbol,
      void (*method)(void*, int, SValue*, SValue*),
      size_t scratchpad_size,
      void (*free_method)(void*));

  inline void call(void* scratchpad, int argc, SValue* argv, SValue* out) const {
    call_(scratchpad, argc, argv, out);
  }

  bool isAggregate() const;
  void (*getFnPtr() const)(void*, int, SValue*, SValue*);
  size_t getScratchpadSize() const;

protected:
  void (*call_)(void*, int, SValue*, SValue*);
  const size_t scratchpad_size_;
};

class SymbolTable : public RefCounted {
public:

  SymbolTableEntry const* lookupSymbol(const std::string& symbol) const;

  SFunction lookup(const String& symbol) const;

  bool isAggregateFunction(const String& symbol) const;

  void registerSymbol(
      const std::string& symbol,
      void (*method)(void*, int, SValue*, SValue*));

  void registerSymbol(
      const std::string& symbol,
      void (*method)(void*, int, SValue*, SValue*),
      size_t scratchpad_size,
      void (*free_method)(void*));

  void registerFunction(
      const String& symbol,
      void (*fn)(int, SValue*, SValue*));

  void registerFunction(
      const String& symbol,
      AggregateFunction fn);

  void registerFunction(
      const String& symbol,
      SFunction fn);

protected:
  std::unordered_map<std::string, SymbolTableEntry> symbols_;
  HashMap<String, SFunction> syms_;
};

}
