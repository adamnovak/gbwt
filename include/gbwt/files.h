/*
  Copyright (c) 2017, 2018 Jouni Siren
  Copyright (c) 2015, 2016, 2017 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef GBWT_FILES_H
#define GBWT_FILES_H

#include <gbwt/utils.h>

namespace gbwt
{

/*
  files.h: Public interface for file formats.
*/

//------------------------------------------------------------------------------

/*
  GBWT file header.

  Version 2:
  - Includes a flag for a bidirectional index.
  - Compatible with version 1.

  Version 1:
  - The first proper version.
  - Identical to version 0.

  Version 0:
  - Preliminary version.
*/

struct GBWTHeader
{
  typedef gbwt::size_type size_type;  // Needed for SDSL serialization.

  std::uint32_t tag;
  std::uint32_t version;
  std::uint64_t sequences;
  std::uint64_t size;           // Including the endmarkers.
  std::uint64_t offset;         // Range [1..offset] of the alphabet is empty.
  std::uint64_t alphabet_size;  // Largest node id + 1.
  std::uint64_t flags;

  constexpr static std::uint32_t TAG = 0x6B376B37;
  constexpr static std::uint32_t VERSION = Version::GBWT_VERSION;
  constexpr static std::uint32_t MIN_VERSION = 1;

  constexpr static std::uint64_t FLAG_MASK          = 0x0001;
  constexpr static std::uint64_t FLAG_BIDIRECTIONAL = 0x0001;

  GBWTHeader();

  size_type serialize(std::ostream& out, sdsl::structure_tree_node* v = nullptr, std::string name = "") const;
  void load(std::istream& in);
  bool check() const;
  bool checkNew() const;

  void setVersion() { this->version = VERSION; }

  void set(std::uint64_t flag) { this->flags |= flag; }
  void unset(std::uint64_t flag) { this->flags &= ~flag; }
  bool get(std::uint64_t flag) const { return (this->flags & flag); }

  void swap(GBWTHeader& another);

  bool operator==(const GBWTHeader& another) const;
  bool operator!=(const GBWTHeader& another) const { return !(this->operator==(another)); }
};

std::ostream& operator<<(std::ostream& stream, const GBWTHeader& header);

//------------------------------------------------------------------------------

} // namespace gbwt

#endif // GBWT_FILES_H
