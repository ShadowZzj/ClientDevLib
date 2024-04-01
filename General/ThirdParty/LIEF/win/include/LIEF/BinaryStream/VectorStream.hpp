/* Copyright 2017 - 2023 R. Thomas
 * Copyright 2017 - 2023 Quarkslab
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef VECTOR_BINARY_STREAM_H
#define VECTOR_BINARY_STREAM_H

#include <vector>
#include <string>

#include "LIEF/errors.hpp"
#include "LIEF/BinaryStream/BinaryStream.hpp"
namespace LIEF {
class VectorStream : public BinaryStream {
  public:
  using BinaryStream::p;
  using BinaryStream::end;
  using BinaryStream::start;

  static result<VectorStream> from_file(const std::string& file);
  VectorStream(std::vector<uint8_t> data);

  VectorStream() = delete;

  // VectorStream should not be copyable for performances reasons
  VectorStream(const VectorStream&) = delete;
  VectorStream& operator=(const VectorStream&) = delete;

  VectorStream(VectorStream&& other);
  VectorStream& operator=(VectorStream&& other);

  uint64_t size() const override {
    return size_;
  }

  const std::vector<uint8_t>& content() const;

  std::vector<uint8_t>&& move_content() {
    size_ = 0;
    return std::move(binary_);
  }

  const uint8_t* p() const override {
    return this->binary_.data() + this->pos();
  }

  const uint8_t* start() const override {
    return this->binary_.data();
  }

  const uint8_t* end() const override {
    return this->binary_.data() + this->binary_.size();
  }

  static bool classof(const BinaryStream& stream);

  protected:
  result<const void*> read_at(uint64_t offset, uint64_t size) const override;
  std::vector<uint8_t> binary_;
  uint64_t size_ = 0; // Original size without alignment
};
}

#endif
