#define POSETS_TRACK_ALLOCATIONS 1
#include "allocation-tracking.hh"
#include "matrix-check.hh"

#include <array>
#include <bit>
#include <iostream>

#include <posets/downsets/sharingtree_backed.hh>
#include <posets/vectors.hh>

namespace {
  using matrix_check::logical;
  using matrix_check::region;
  using matrix_check::require;

  template <class V>
  struct step {
      region input, expected;
      std::vector<V> queries;
      std::vector<bool> answers;
      bool unite;
  };

  template <class V>
  std::vector<step<V>> trace (size_t rounds, size_t dim, bool changing) {
    std::vector<step<V>> result;
    region current;
    for (size_t round = 0; round < rounds; ++round) {
      const int offset = changing ? static_cast<int> (8 * round) : 0;
      region band;
      for (int i = 0; i < 8; ++i) {
        logical value (dim, 2);
        value[0] = offset + i;
        value[1] = -offset - i;
        for (size_t j = 2; j < dim; ++j)
          value[j] = i == 0 ? 2 : static_cast<int> ((i + j) % 4) - 1;
        band.push_back (std::move (value));
      }
      band.push_back (band.front ());  // exercise duplicate and dominated inputs
      auto dominated = band.front ();
      --dominated[0];
      band.push_back (std::move (dominated));
      logical cap (dim, 2);
      cap[0] = offset + 7;
      cap[1] = -offset;
      for (bool unite : {true, false}) {
        region input = unite ? band : region {cap};
        region next;
        if (unite) {
          next = current;
          next.insert (next.end (), input.begin (), input.end ());
        }
        else {
          for (const auto& a : current)
            for (const auto& b : input) {
              logical meet;
              for (size_t j = 0; j < dim; ++j)
                meet.push_back (std::min (a[j], b[j]));
              next.push_back (std::move (meet));
            }
        }
        const auto maxima = matrix_check::maxima (next);
        current.assign (maxima.begin (), maxima.end ());
        step<V> entry {input, current, {}, {}, unite};
        region probes = current;
        probes.insert (probes.end (), input.begin (), input.end ());
        probes.emplace_back (dim, -100000);
        probes.emplace_back (dim, 100000);
        auto miss = cap;
        ++miss[0];
        probes.push_back (std::move (miss));
        for (const auto& query : probes) {
          entry.queries.push_back (matrix_check::vector<V> (query));
          entry.answers.push_back (std::ranges::any_of (
              current, [&] (const auto& row) { return matrix_check::below (query, row); }));
        }
        result.push_back (std::move (entry));
      }
    }
    return result;
  }

  template <class S>
  void check (const S& actual, const step<typename S::value_type>& expected) {
    // The scalar oracle and probes were allocated before tracking. Validation
    // allocates only the backend's own iteration/size/membership caches.
    std::array<bool, 16> seen {};
    require (expected.expected.size () <= seen.size (), "fixture frontier grew");
    size_t count = 0;
    for (const auto& value : actual) {
      size_t index = 0;
      for (; index < expected.expected.size (); ++index) {
        const auto& row = expected.expected[index];
        bool equal = value.size () == row.size ();
        for (size_t j = 0; equal and j < row.size (); ++j)
          equal = value[j] == row[j];
        if (equal)
          break;
      }
      require (index < expected.expected.size (), "incorrect maximal generator");
      require (not seen[index], "duplicate maximal generator");
      seen[index] = true;
      ++count;
    }
    require (count == expected.expected.size () and actual.size () == count,
             "incorrect maximal generator count");
    for (size_t i = 0; i < expected.queries.size (); ++i)
      require (actual.contains (expected.queries[i]) == expected.answers[i],
               "membership differs from independent scalar oracle");
  }

  template <class V>
  bool run (const char* vector, size_t rounds, size_t dim, bool changing) {
    using S = posets::downsets::sharingtree_backed<V>;
    const auto steps = trace<V> (rounds, dim, changing);
    // Include the persistent weak-map entry/control block in both baselines.
    {
      const auto warm = matrix_check::build<S> (steps.front ().input);
    }
    const size_t baseline = allocation_tracking::live;
    allocation_tracking::reset_peak ();
    struct sample {
        size_t rounds, retained, peak, allocations;
    };
    std::array<sample, 16> samples {};
    size_t count = 0;
    {
      const auto witness = matrix_check::build<S> (steps.front ().input);
      check (witness, steps.front ());
      auto subject = matrix_check::build<S> ({});
      for (size_t i = 0; i < steps.size (); ++i) {
        {
          auto operand = matrix_check::build<S> (steps[i].input);
          if (steps[i].unite)
            subject.union_with (std::move (operand));
          else
            subject.intersect_with (operand);
        }
        check (subject, steps[i]);
        check (witness, steps.front ());  // another live root must survive reuse
        const size_t completed = (i + 1) / 2;
        if (i % 2 != 0 and (std::has_single_bit (completed) or completed == rounds))
          samples[count++] = {completed, allocation_tracking::live - baseline,
                              allocation_tracking::peak - baseline, allocation_tracking::count};
      }
    }
    require (allocation_tracking::live == baseline, "forest retained bytes after last owner died");
    const auto state = std::cout.rdstate ();
    std::cout.clear ();
    for (size_t i = 0; i < count; ++i)
      std::cout << "vector=" << vector << " changing=" << changing << " dim=" << dim
                << " rounds=" << samples[i].rounds << " retained=" << samples[i].retained
                << " peak=" << samples[i].peak << " allocations=" << samples[i].allocations
                << " released=all oracle=PASS\n";
    std::cout.setstate (state);
    // Repeated inputs may retain small algebra drafts, but rebuilding duplicate
    // suffixes must not retain a dimension-sized span per vector per operation.
    return changing or samples[count - 1].retained <= samples[0].retained + 1024 * rounds;
  }
}

int main (int argc, char** argv) try {
  const size_t rounds = argc > 1 ? std::stoul (argv[1]) : 128;
  const size_t dim = argc > 2 ? std::stoul (argv[2]) : 17;
  require (argc <= 3 and rounds > 0 and rounds <= 4096 and dim >= 2 and dim <= 129,
           "usage: sharing-forest-lifetime [rounds:1..4096] [dimension:2..129]");
  std::cout.setstate (std::ios::failbit);  // suppress debug narration, retain assertions
  bool bounded = true;
  for (bool changing : {false, true}) {
    bounded &= run<posets::vectors::vector_backed<int32_t>> ("scalar", rounds, dim, changing);
    bounded &= run<posets::vectors::simd_vector_backed<int32_t>> ("simd", rounds, dim, changing);
  }
  // Report every fixture before rejecting a baseline or candidate's footprint.
  require (bounded, "duplicate forest spans retained across repeated operations");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
