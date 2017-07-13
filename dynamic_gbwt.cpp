/*
  Copyright (c) 2017 Genome Research Ltd.

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

#include "dynamic_gbwt.h"
#include "internal.h"

namespace gbwt
{

//------------------------------------------------------------------------------

const std::string DynamicGBWT::EXTENSION = ".gbwt";

DynamicGBWT::DynamicGBWT()
{
}

DynamicGBWT::DynamicGBWT(const DynamicGBWT& source)
{
  this->copy(source);
}

DynamicGBWT::DynamicGBWT(DynamicGBWT&& source)
{
  *this = std::move(source);
}

DynamicGBWT::~DynamicGBWT()
{
}

void
DynamicGBWT::swap(DynamicGBWT& another)
{
  if(this != &another)
  {
    this->header.swap(another.header);
    this->bwt.swap(another.bwt);
  }
}

DynamicGBWT&
DynamicGBWT::operator=(const DynamicGBWT& source)
{
  if(this != &source) { this->copy(source); }
  return *this;
}

DynamicGBWT&
DynamicGBWT::operator=(DynamicGBWT&& source)
{
  if(this != &source)
  {
    this->header = std::move(source.header);
    this->bwt = std::move(source.bwt);
  }
  return *this;
}

DynamicGBWT::size_type
DynamicGBWT::serialize(std::ostream& out, sdsl::structure_tree_node* v, std::string name) const
{
  sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
  size_type written_bytes = 0;

  written_bytes += this->header.serialize(out);

  std::vector<byte_type> compressed_bwt;
  std::vector<size_type> bwt_offsets(this->bwt.size());
  for(comp_type comp = 0; comp < this->effective(); comp++)
  {
    bwt_offsets[comp] = compressed_bwt.size();
    const DynamicRecord& current = this->bwt[comp];

    // Write the outgoing edges.
    ByteCode::write(compressed_bwt, current.outdegree());
    for(edge_type outedge : current.outgoing)
    {
      ByteCode::write(compressed_bwt, outedge.first);
      ByteCode::write(compressed_bwt, outedge.second);
    }

    // Write the body.
    if(current.outdegree() > 0)
    {
      Run encoder(current.outdegree());
      for(run_type run : current.body) { encoder.write(compressed_bwt, run); }
    }
  }

  // Build and serialize index.
  sdsl::sd_vector_builder builder(compressed_bwt.size(), bwt_offsets.size());
  for(size_type offset : bwt_offsets) { builder.set(offset); }
  sdsl::sd_vector<> node_index(builder);
  sdsl::sd_vector<>::select_1_type node_select(&node_index);
  written_bytes += node_index.serialize(out);
  written_bytes += node_select.serialize(out);

  // Serialize BWT.
  out.write((char*)(compressed_bwt.data()), compressed_bwt.size());
  written_bytes += compressed_bwt.size();

  sdsl::structure_tree::add_size(child, written_bytes);
  return written_bytes;
}

void
DynamicGBWT::load(std::istream& in)
{
  // Read the header.
  this->header.load(in);
  if(!(this->header.check()))
  {
    std::cerr << "DynamicGBWT::load(): Invalid header: " << this->header << std::endl;
  }
  this->bwt.resize(this->effective());

  // Read the node index.
  sdsl::sd_vector<> node_index; node_index.load(in);
  sdsl::sd_vector<>::select_1_type node_select; node_select.load(in, &node_index);

  for(comp_type comp = 0; comp < this->effective(); comp++)
  {
    DynamicRecord& current = this->bwt[comp];
    current.clear();

    // Read the current node.
    size_type start = node_select(comp + 1);
    size_type stop = (comp + 1 < this->effective() ? node_select(comp + 2) : node_index.size());
    std::vector<byte_type> node_encoding(stop - start);
    in.read((char*)(node_encoding.data()), node_encoding.size());
    size_type offset = 0;

    // Decompress the outgoing edges.
    current.outgoing.resize(ByteCode::read(node_encoding, offset));
    for(edge_type& outedge : current.outgoing)
    {
      outedge.first = ByteCode::read(node_encoding, offset);
      outedge.second = ByteCode::read(node_encoding, offset);
    }

    // Decompress the body.
    current.body.clear();
    if(current.outdegree() > 0)
    {
      Run decoder(current.outdegree());
      while(offset < node_encoding.size())
      {
        run_type run = decoder.read(node_encoding, offset);
        current.body.push_back(run);
        current.body_size += run.second;
      }
    }
  }

  // Rebuild the incoming edges.
  for(comp_type comp = 0; comp < this->effective(); comp++)
  {
    DynamicRecord& current = this->bwt[comp];
    std::vector<size_type> counts(current.outdegree());
    for(run_type run : current.body) { counts[run.first] += run.second; }
    for(rank_type outrank = 0; outrank < current.outdegree(); outrank++)
    {
      if(current.successor(outrank) != ENDMARKER)
      {
        DynamicRecord& successor = this->record(current.successor(outrank));
        successor.addIncoming(edge_type(comp, counts[outrank]));
      }
    }
  }
}

void
DynamicGBWT::copy(const DynamicGBWT& source)
{
  this->header = source.header;
  this->bwt = source.bwt;
}

//------------------------------------------------------------------------------

size_type
DynamicGBWT::runs() const
{
  size_type total = 0;
  for(const DynamicRecord& node : this->bwt) { total += node.runs(); }
  return total;
}

bool
DynamicGBWT::compare(const DynamicGBWT& another, std::ostream& out) const
{
  out << "Comparing dynamic GBWTs" << std::endl;
  out << std::endl;

  if(this->header != another.header)
  {
    out << "This:    " << this->header << std::endl;
    out << "Another: " << another.header << std::endl;
    out << std::endl;
    return false;
  }

  for(comp_type comp = 0; comp < this->effective(); comp++)
  {
    if(this->bwt[comp] != another.bwt[comp])
    {
      out << "This[" << comp << "]:    " << this->bwt[comp] << std::endl;
      out << "Another[" << comp << "]: " << another.bwt[comp] << std::endl;
      out << std::endl;
      return false;
    }
  }

  out << "The GBWTs are identical" << std::endl;
  out << std::endl;
  return true;
}

//------------------------------------------------------------------------------

/*
  Support functions for index construction.
*/

void
swapBody(DynamicRecord& record, RunMerger& merger)
{
  merger.flush();
  merger.runs.swap(record.body);
  std::swap(merger.total_size, record.body_size);
}

void
DynamicGBWT::resize(size_type new_offset, size_type new_sigma)
{
  /*
    Do not set the new offset, if we already have a smaller real offset or the
    new offset is not a real one.
  */
  if((this->sigma() > 1 && new_offset > this->header.offset) || new_sigma <= 1)
  {
    new_offset = this->header.offset;
  }
  if(this->sigma() > new_sigma) { new_sigma = this->sigma(); }
  if(new_offset > 0 && new_offset >= new_sigma)
  {
    std::cerr << "DynamicGBWT::resize(): Cannot set offset " << new_offset << " with alphabet size " << new_sigma << std::endl;
    std::exit(EXIT_FAILURE);
  }

  if(new_offset != this->header.offset || new_sigma != this->sigma())
  {
    if(Verbosity::level >= Verbosity::FULL)
    {
      if(new_offset != this->header.offset)
      {
        std::cerr << "DynamicGBWT::resize(): Changing alphabet offset to " << new_offset << std::endl;
      }
      if(new_sigma != this->sigma())
      {
        std::cerr << "DynamicGBWT::resize(): Increasing alphabet size to " << new_sigma << std::endl;
      }
    }

    std::vector<DynamicRecord> new_bwt(new_sigma - new_offset);
    if(this->effective() > 0) { new_bwt[0].swap(this->bwt[0]); }
    for(comp_type comp = 1; comp < this->effective(); comp++)
    {
      new_bwt[comp + this->header.offset - new_offset].swap(this->bwt[comp]);
    }
    this->bwt.swap(new_bwt);
    this->header.offset = new_offset; this->header.alphabet_size = new_sigma;
  }
}

void
nextPosition(std::vector<Sequence>& seqs, const text_type&)
{
  for(Sequence& seq : seqs) { seq.pos++; }
}

void
nextPosition(std::vector<Sequence>& seqs, const DynamicGBWT& source)
{
  for(size_type i = 0; i < seqs.size(); )
  {
    node_type curr = seqs[i].curr;
    const DynamicRecord& current = source.record(curr);
    std::vector<run_type>::const_iterator iter = current.body.begin();
    std::vector<edge_type> result(current.outgoing);
    size_type offset = iter->second; result[iter->first].second += iter->second;
    while(i < seqs.size() && seqs[i].curr == curr)
    {
      while(offset <= seqs[i].pos)
      {
        ++iter; offset += iter->second;
        result[iter->first].second += iter->second;
      }
      seqs[i].pos = result[iter->first].second - (offset - seqs[i].pos);
      i++;
    }
  }
}

void
advancePosition(std::vector<Sequence>& seqs, const text_type& text)
{
  for(Sequence& seq : seqs) { seq.curr = seq.next; seq.next = text[seq.pos]; }
}

void
advancePosition(std::vector<Sequence>& seqs, const DynamicGBWT& source)
{
  // FIXME We could optimize further by storing the next position.
  for(size_type i = 0; i < seqs.size(); )
  {
    node_type curr = seqs[i].next;
    const DynamicRecord& current = source.record(curr);
    std::vector<run_type>::const_iterator iter = current.body.begin();
    size_type offset = iter->second;
    while(i < seqs.size() && seqs[i].next == curr)
    {
      seqs[i].curr = seqs[i].next;
      while(offset <= seqs[i].pos) { ++iter; offset += iter->second; }
      seqs[i].next = current.successor(iter->first);
      i++;
    }
  }
}

void
DynamicGBWT::recode()
{
  #pragma omp parallel for schedule(static)
  for(comp_type comp = 0; comp < this->effective(); comp++) { this->bwt[comp].recode(); }
}

//------------------------------------------------------------------------------

/*
  Insert the sequences from the source to the GBWT. Maintains an invariant that
  the sequences are sorted by (curr, offset).
*/

template<class Source>
size_type
insert(DynamicGBWT& gbwt, std::vector<Sequence>& seqs, const Source& source)
{
  size_type iterations = 0;
  while(true)
  {
    iterations++;  // We use 1-based iterations.

    /*
      Process ranges of sequences sharing the same 'curr' node.
      - Add the outgoing edge (curr, next) if necessary.
      - Insert the 'next' node into position 'offset' in the body.
      - Set 'offset' to rank(next) within the record.
      - Update the predecessor count of 'curr' in the incoming edges of 'next'.

      We do not maintain incoming edges to the endmarker, because it can be expensive
      and because searching with the endmarker does not work in a multi-string BWT.
    */
    for(size_type i = 0; i < seqs.size(); )
    {
      node_type curr = seqs[i].curr;
      DynamicRecord& current = gbwt.record(curr);
      RunMerger new_body(current.outdegree());
      std::vector<run_type>::iterator iter = current.body.begin();
      while(i < seqs.size() && seqs[i].curr == curr)
      {
        rank_type outrank = current.edgeTo(seqs[i].next);
        if(outrank >= current.outdegree())  // Add edge (curr, next) if it does not exist.
        {
          current.outgoing.push_back(edge_type(seqs[i].next, 0));
          new_body.addEdge();
        }
        while(new_body.size() < seqs[i].offset)  // Add old runs until 'offset'.
        {
          if(iter->second <= seqs[i].offset - new_body.size()) { new_body.insert(*iter); ++iter; }
          else
          {
            run_type temp(iter->first, seqs[i].offset - new_body.size());
            new_body.insert(temp);
            iter->second -= temp.second;
          }
        }
        seqs[i].offset = new_body.counts[outrank]; // rank(next) within the record.
        new_body.insert(outrank);
        if(seqs[i].next != ENDMARKER)  // The endmarker does not have incoming edges.
        {
          gbwt.record(seqs[i].next).increment(curr);
        }
        i++;
      }
      while(iter != current.body.end()) // Add the rest of the old body.
      {
        new_body.insert(*iter); ++iter;
      }
      swapBody(current, new_body);
    }
    gbwt.header.size += seqs.size();
    nextPosition(seqs, source); // Determine the next position for each sequence.

    /*
      Sort the sequences for the next iteration and remove the ones that have reached the endmarker.
      Note that sorting by (next, curr, offset) now is equivalent to sorting by (curr, offset) in the
      next interation.
    */
    chooseBestSort(seqs.begin(), seqs.end());
    size_type head = 0;
    while(head < seqs.size() && seqs[head].next == ENDMARKER) { head++; }
    if(head > 0)
    {
      for(size_type j = 0; head + j < seqs.size(); j++) { seqs[j] = seqs[head + j]; }
      seqs.resize(seqs.size() - head);
    }
    if(seqs.empty()) { return iterations; }

    /*
      Rebuild the edge offsets in the outgoing edges to each 'next' node. The offsets will be
      valid after the insertions in the next iteration.
    */
    node_type next = gbwt.sigma();
    for(Sequence& seq : seqs)
    {
      if(seq.next == next) { continue; }
      next = seq.next;
      size_type offset = 0;
      for(edge_type inedge : gbwt.record(next).incoming)
      {
        DynamicRecord& predecessor = gbwt.record(inedge.first);
        predecessor.offset(predecessor.edgeTo(next)) = offset;
        offset += inedge.second;
      }
    }

    /*
      Until now sequence offsets have been rank(next) within the record. We add edge offsets
      to them to get valid offsets in the next record and then advance the text position.
    */
    for(Sequence& seq : seqs)
    {
      const DynamicRecord& current = gbwt.record(seq.curr);
      seq.offset += current.offset(current.edgeTo(seq.next));
    }
    advancePosition(seqs, source);  // Move each sequence to the next position.
  }
}

//------------------------------------------------------------------------------

void
DynamicGBWT::insert(const text_type& text)
{
  double start = readTimer();

  if(text.empty()) { return; }
  if(text[text.size() - 1] != ENDMARKER)
  {
    std::cerr << "DynamicGBWT::insert(): The text must end with an endmarker" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  /*
    Find the start of each sequence and initialize the sequence objects at the endmarker node.
    Increase alphabet size and decrease offset if necessary.
  */
  bool seq_start = true;
  node_type min_node = (this->empty() ? ~(node_type)0 : this->header.offset + 1);
  node_type max_node = (this->empty() ? 0 : this->sigma() - 1);
  std::vector<Sequence> seqs;
  for(size_type i = 0; i < text.size(); i++)
  {
    if(seq_start)
    {
      seqs.push_back(Sequence(text, i, this->sequences()));
      seq_start = false; this->header.sequences++;
    }
    if(text[i] == ENDMARKER) { seq_start = true; }
    else { min_node = std::min(text[i], min_node); }
    max_node = std::max(text[i], max_node);
  }
  if(Verbosity::level >= Verbosity::EXTENDED)
  {
    std::cerr << "DynamicGBWT::insert(): Inserting " << seqs.size() << " sequences of total length " << text.size() << std::endl;
  }
  if(max_node == 0) { min_node = 1; } // No real nodes, setting offset to 0.
  this->resize(min_node - 1, max_node + 1);

  // Insert the sequences and sort the outgoing edges.
  size_type iterations = gbwt::insert(*this, seqs, text);
  this->recode();

  if(Verbosity::level >= Verbosity::EXTENDED)
  {
    double seconds = readTimer() - start;
    std::cerr << "DynamicGBWT::insert(): " << iterations << " iterations in " << seconds << " seconds" << std::endl;
  }
}

//------------------------------------------------------------------------------

void
DynamicGBWT::merge(const DynamicGBWT& source, size_type batch_size)
{
  double start = readTimer();

  if(source.empty())
  {
    if(Verbosity::level >= Verbosity::EXTENDED)
    {
      std::cerr << "DynamicGBWT::merge(): The other GBWT is empty" << std::endl;
    }
    return;
  }
  if(this->empty())
  {
    *this = source;
    if(Verbosity::level >= Verbosity::EXTENDED)
    {
      double seconds = readTimer() - start;
      std::cerr << "DynamicGBWT::merge(): Inserted " << source.sequences() << " sequences of total length "
                << source.size() << " into an empty GBWT in " << seconds << " seconds" << std::endl;
    }
    return;
  }

  // Increase alphabet size and decrease offset if necessary.
  if(batch_size == 0) { batch_size = source.sequences(); }
  this->resize(source.header.offset, source.sigma());

  // Insert the sequences in batches.
  const DynamicRecord& endmarker = source.record(ENDMARKER);
  std::vector<run_type>::const_iterator iter = endmarker.body.begin();
  size_type source_offset = 0, run_offset = 0;
  while(source_offset < source.sequences())
  {
    double batch_start = readTimer();
    size_type limit = std::min(source_offset + batch_size, source.sequences());
    std::vector<Sequence> seqs; seqs.reserve(limit - source_offset);
    while(source_offset < limit)  // Create the new sequence iterators.
    {
      if(run_offset >= iter->second) { ++iter; run_offset = 0; }
      else
      {
        seqs.push_back(Sequence(endmarker.successor(iter->first), this->sequences(), source_offset));
        this->header.sequences++; source_offset++; run_offset++;
      }
    }
    if(Verbosity::level >= Verbosity::EXTENDED)
    {
      std::cerr << "DynamicGBWT::merge(): Inserting sequences " << (source_offset - seqs.size())
                << " to " << (source_offset - 1) << std::endl;
    }
    size_type iterations = gbwt::insert(*this, seqs, source);
    if(Verbosity::level >= Verbosity::EXTENDED)
    {
      double seconds = readTimer() - batch_start;
      std::cerr << "DynamicGBWT::merge(): " << iterations << " iterations in " << seconds << " seconds" << std::endl;
    }
  }

  // Finally sort the outgoing edges.
  this->recode();

  if(Verbosity::level >= Verbosity::BASIC)
  {
    double seconds = readTimer() - start;
    std::cerr << "DynamicGBWT::merge(): Inserted " << source.sequences() << " sequences of total length "
              << source.size() << " in " << seconds << " seconds" << std::endl;
  }
}

//------------------------------------------------------------------------------

size_type
DynamicGBWT::LF(node_type from, size_type i, node_type to) const
{
  if(to >= this->sigma()) { return invalid_offset(); }
  if(from >= this->sigma()) { return this->count(to); }

  size_type result = this->record(from).LF(i, to);
  if(result != invalid_offset()) { return result; }

  /*
    Edge (from, to) has not been observed. We find the first edge from a node >= 'from' to 'to'.
    If 'inrank' is equal to indegree, all incoming edges are from nodes < 'from'.
    Otherwise the result is the stored offset in the node we found.
  */
  const DynamicRecord& to_node = this->record(to);
  rank_type inrank = to_node.findFirst(from);
  if(inrank >= to_node.indegree()) { return this->count(to); }
  const DynamicRecord& next_from = this->record(to_node.predecessor(inrank));
  return next_from.offset(next_from.edgeTo(to));
}

edge_type
DynamicGBWT::LF(node_type from, size_type i) const
{
  if(from >= this->sigma()) { return invalid_edge(); }
  return this->record(from).LF(i);
}

//------------------------------------------------------------------------------

} // namespace gbwt
