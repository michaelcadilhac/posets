#pragma once

#include "matrix-check.hh"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <tuple>

namespace matrix_cpre {
  using matrix_check::logical;
  using matrix_check::region;
  using matrix_check::require;
  using action = std::vector<std::tuple<size_t, size_t, int>>;

  inline int64_t integer (const std::string& text) {
    int64_t value;
    const auto [end, error] = std::from_chars (text.data (), text.data () + text.size (), value);
    require (error == std::errc {} and end == text.data () + text.size (),
             "invalid trace integer");
    return value;
  }
  inline std::vector<std::string> fields (const std::string& line) {
    std::istringstream stream (line);
    std::vector<std::string> result;
    for (std::string field; std::getline (stream, field, '\t');)
      result.push_back (field);
    if (not line.empty () and line.back () == '\t')
      result.emplace_back ();
    return result;
  }
  inline std::ifstream complete_file (const std::filesystem::path& path) {
    require (std::filesystem::is_regular_file (std::filesystem::symlink_status (path)),
             "nonregular trace file");
    std::ifstream input (path, std::ios::binary);
    input.seekg (0, std::ios::end);
    require (input.tellg () > 0, "empty trace file");
    input.seekg (-1, std::ios::end);
    char last;
    input.get (last);
    require (last == '\n', "truncated trace file");
    input.seekg (0);
    return input;
  }
  struct trace {
      size_t dimension = 0, numeric = 0, initial = 0;
      int k = 0;
      region before, after;
      std::vector<action> actions;
      bool membership = false, expected_answer = false;
      logical query;
      std::string origin;
  };

  inline trace load (const std::filesystem::path& path) {
    trace result;
    auto meta = complete_file (path.parent_path () / "meta.tsv");
    std::string line;
    require (bool (std::getline (meta, line)), "missing trace metadata");
    const auto names = fields (line);
    require (bool (std::getline (meta, line)), "missing trace metadata row");
    const auto values = fields (line);
    require (values.size () == names.size (), "malformed trace metadata");
    std::map<std::string, std::string> metadata;
    for (size_t i = 0; i < names.size (); ++i)
      require (metadata.emplace (names[i], values[i]).second, "duplicate metadata key");
    require (not std::getline (meta, line), "multiple trace metadata rows");
    require (integer (metadata.at ("schema_version")) == 3, "unsupported trace directory schema");
    const auto dimension = integer (metadata.at ("states"));
    const auto numeric = integer (metadata.at ("bool_threshold"));
    const auto initial = integer (metadata.at ("init_state"));
    require (dimension > 0 and numeric >= 0 and numeric <= dimension and initial >= 0 and
                 initial < dimension,
             "invalid trace dimension/Boolean suffix/initial state");
    result.dimension = size_t (dimension);
    result.numeric = size_t (numeric);
    result.initial = size_t (initial);

    auto input = complete_file (path);
    require (bool (std::getline (input, line)) and line.starts_with ("# "), "missing CPre header");
    std::map<std::string, int64_t> header;
    std::istringstream words (line.substr (2));
    for (std::string word; words >> word;) {
      const auto equal = word.find ('=');
      require (equal != std::string::npos, "malformed CPre header");
      const auto name = word.substr (0, equal);
      if (name == "origin") {
        result.origin = word.substr (equal + 1);
        require (result.origin == "picker" or result.origin == "picker-pq" or
                     result.origin == "initial",
                 "unknown query origin");
        require (not(words >> word), "trailing contains metadata");
        break;
      }
      if (name == "input")
        break;  // BDD text is provenance, never parsed as an operation.
      require (header.emplace (name, integer (word.substr (equal + 1))).second,
               "duplicate CPre header key");
    }
    const auto bound = header.at ("k");
    require (bound > 0 and bound <= 127, "trace bound exceeds signed8-bit domain");
    result.k = int (bound);
    if (header.at ("schema_version") == 1) {
      result.membership = true;
      require (header.size () == 8 and not result.origin.empty () and header.at ("maxima") >= 0 and
                   (header.at ("expected") == 0 or header.at ("expected") == 1),
               "malformed contains header");
      result.expected_answer = header.at ("expected") != 0;
      const auto ordinal = header.at ("ordinal");
      const auto hits = header.at ("hits");
      size_t bin = 0;
      for (const auto mark : {16, 64, 256, 1024, 4096, 16384})
        bin += size_t (header.at ("maxima") >= mark);
      require (header.at ("loop") >= 0 and header.at ("bin") == int64_t (bin) and ordinal > 0 and
                   hits >= 0 and hits <= ordinal and
                   (result.expected_answer ? hits > 0 : hits < ordinal),
               "inconsistent query ordinal/bin/hits");
      require (bool (std::getline (input, line)) and line == "[query]", "missing query section");
      require (bool (std::getline (input, line)), "missing query rank");
      for (const auto& value : fields (line)) {
        const auto coordinate = integer (value);
        // Actual forward queries can exceed zero in the Boolean suffix: those
        // are valid membership misses, not values to coerce into packed bits.
        require (coordinate >= -1 and coordinate <= result.k, "query out of rank bounds");
        result.query.push_back (int (coordinate));
      }
      require (result.query.size () == result.dimension, "query dimension mismatch");
      require (bool (std::getline (input, line)) and line == "[generators]",
               "missing generator section");
      bool ended = false;
      while (std::getline (input, line)) {
        if (line == "[end]") {
          ended = true;
          break;
        }
        logical rank;
        const auto columns = fields (line);
        require (columns.size () == result.dimension, "generator dimension mismatch");
        for (size_t i = 0; i < columns.size (); ++i) {
          const auto coordinate = integer (columns[i]);
          require (coordinate >= -1 and coordinate <= (i < result.numeric ? result.k - 1 : 0),
                   "generator out of numeric/Boolean bounds");
          rank.push_back (int (coordinate));
        }
        result.before.push_back (std::move (rank));
      }
      require (ended and not std::getline (input, line) and
                   result.before.size () == size_t (header.at ("maxima")),
               "incomplete/trailing query trace");
      const bool answer = std::ranges::any_of (result.before, [&] (const auto& value) {
        return matrix_check::below (result.query, value);
      });
      require (answer == result.expected_answer,
               "source contains answer disagrees with scalar oracle");
      return result;
    }
    require (header.size () == 5 and header.at ("schema_version") == 2 and
                 header.at ("loop") >= 0 and header.at ("before") > 0 and
                 header.at ("actions") >= 0,
             "unsupported or empty CPre trace");
    int section = 0;
    int64_t after_count = -1;
    while (std::getline (input, line)) {
      if (line == "[before]") {
        require (section == 0, "duplicate/out-of-order before section");
        section = 1;
      }
      else if (line == "[actions]") {
        require (section == 1, "out-of-order actions section");
        section = 2;
      }
      else if (line.starts_with ("[after]\t")) {
        require (section == 2, "out-of-order after section");
        section = 3;
        after_count = integer (line.substr (8));
        require (after_count >= 0, "negative after count");
      }
      else if (line.starts_with ("action\t")) {
        require (section == 2 and integer (line.substr (7)) == int (result.actions.size ()),
                 "action index gap");
        result.actions.emplace_back ();
      }
      else {
        const auto columns = fields (line);
        if (section == 2) {
          require (not result.actions.empty () and columns.size () == 3, "malformed action edge");
          const auto destination = integer (columns[0]);
          const auto source = integer (columns[1]);
          const auto increment = integer (columns[2]);
          require (destination >= 0 and destination < dimension and source >= 0 and
                       source < dimension and (increment == 0 or increment == 1),
                   "action edge out of bounds");
          result.actions.back ().emplace_back (size_t (destination), size_t (source), increment);
        }
        else {
          require ((section == 1 or section == 3) and columns.size () == result.dimension,
                   "malformed rank row");
          logical rank;
          for (size_t i = 0; i < columns.size (); ++i) {
            const auto value = integer (columns[i]);
            require (value >= -1 and value <= (i < result.numeric ? result.k - 1 : 0),
                     "rank outside numeric/Boolean domain");
            rank.push_back (int (value));
          }
          (section == 1 ? result.before : result.after).push_back (std::move (rank));
        }
      }
    }
    require (section == 3 and result.before.size () == size_t (header.at ("before")) and
                 result.actions.size () == size_t (header.at ("actions")) and
                 result.after.size () == size_t (after_count),
             "incomplete CPre trace");
    return result;
  }

  inline logical predecessor (const logical& value, const action& edges, const trace& input) {
    logical image (input.dimension, 0);
    std::ranges::fill (std::span (image).first (input.numeric), input.k - 1);
    for (const auto& [destination, source, increment] : edges)
      image[source] = std::min (image[source], std::max (-1, value[destination] - increment));
    return image;
  }
  inline void insert (region& result, logical value) {
    if (std::ranges::any_of (result,
                             [&] (const auto& v) { return matrix_check::below (value, v); }))
      return;
    std::erase_if (result, [&] (const auto& v) { return matrix_check::below (v, value); });
    result.push_back (std::move (value));
  }
  inline void reference (const trace& input) {
    // Production retains its explicit bottom accumulator when no output action
    // exists. Empty actions are distinct from an empty region/zero dimension.
    region preimages {logical (input.dimension, -1)};
    region expected;
    for (const auto& edges : input.actions)
      for (const auto& value : input.before)
        insert (preimages, predecessor (value, edges, input));
    for (const auto& a : input.before)
      for (const auto& b : preimages) {
        logical met;
        for (size_t i = 0; i < a.size (); ++i)
          met.push_back (std::min (a[i], b[i]));
        insert (expected, std::move (met));
      }
    require (std::set<logical> (expected.begin (), expected.end ()) ==
                 std::set<logical> (input.after.begin (), input.after.end ()),
             "source CPre trace disagrees with scalar oracle");
  }
  inline void validate_source (const trace& input) {
    require (matrix_check::maxima (input.before).size () == input.before.size (),
             "noncanonical source generators");
    if (not input.membership) {
      require (matrix_check::maxima (input.after).size () == input.after.size (),
               "noncanonical source result");
      reference (input);
    }
  }
  template <class S>
  std::map<std::string, double> run (const std::filesystem::path& path, size_t rounds,
                                     bool verify_only, bool detail, bool source_verified = false) {
    const auto input = load (path);
    if (verify_only and not source_verified)
      validate_source (input);
    posets::vectors::bool_threshold = posets::vectors::bitset_threshold = input.numeric;
    using vector_type = typename S::value_type;
    std::map<std::string, double> result;
    const auto timed = [&] (const std::string& key, auto work) {
      if (verify_only or
          (not detail and (key == "apply" or key == "union" or key == "intersection"))) {
        work ();
        return;
      }
      const auto wall = std::chrono::steady_clock::now ();
      const auto cpu = std::clock ();
      CALLGRIND_START_INSTRUMENTATION;
      work ();
      CALLGRIND_STOP_INSTRUMENTATION;
      const auto wall_end = std::chrono::steady_clock::now ();
      const auto cpu_end = std::clock ();
      result[key] += std::chrono::duration<double> (wall_end - wall).count ();
      result[key + "_cpu"] += double (cpu_end - cpu) / CLOCKS_PER_SEC;
    };
    require (rounds > 0, "zero replay rounds");
    if (input.membership) {
      for (size_t round = 0; round < rounds; ++round) {
        std::optional<S> region;
        timed ("build", [&] { region.emplace (matrix_check::build<S> (input.before)); });
        const auto query = matrix_check::vector<vector_type> (input.query);
        bool answer = false;
        timed ("contains", [&] { answer = region->contains (query); });
        require (answer == input.expected_answer,
                 "backend contains answer disagrees with source/scalar oracle");
        std::optional<S> rebuilt;
        timed ("rebuild", [&] { rebuilt.emplace (matrix_check::build<S> (input.before)); });
      }
      if (not verify_only) {
        result["rounds"] = double (rounds);
        result["dimension"] = double (input.dimension);
        result["boolean_tail"] = double (input.dimension - input.numeric);
        result["before_maxima"] = double (input.before.size ());
        result["actual_query"] = 1;
        result["expected_answer"] = input.expected_answer;
      }
      return result;
    }
    posets::utils::vector_mm<typename vector_type::value_type> reset (input.dimension, 0);
    posets::utils::vector_mm<typename vector_type::value_type> image;
    std::fill (reset.begin (), reset.begin () + input.numeric, input.k - 1);
    image.reserve (input.dimension);
    const std::set<logical> expected (input.after.begin (), input.after.end ());
    require (expected.size () == input.after.size (), "duplicate source maxima");

    for (size_t round = 0; round < rounds; ++round) {
      std::optional<S> frontier;
      timed ("build", [&] { frontier.emplace (matrix_check::build<S> (input.before)); });
      std::chrono::steady_clock::time_point start;
      std::clock_t cpu = 0;
      if (not verify_only) {
        start = std::chrono::steady_clock::now ();
        cpu = std::clock ();
        if (not detail)
          CALLGRIND_START_INSTRUMENTATION;
      }
      {
        // Production constructs the bottom accumulator before replacing it
        // with the first mapped set. Include its construction and all CPre
        // temporary destructors, as the production scoped timer does.
        posets::utils::vector_mm<typename vector_type::value_type> bottom (input.dimension, -1);
        std::optional<S> combined;
        combined.emplace (vector_type (bottom));
        bool first = true;
        for (const auto& edges : input.actions) {
          std::optional<S> mapped;
          timed ("apply", [&] {
            mapped.emplace (frontier->apply ([&] (const vector_type& value) {
              // Match the production actioner's retained reset/output scratch;
              // do not add an unpack/allocation per maximum to the measured map.
              image = reset;
              for (const auto& [destination, source, increment] : edges)
                if (image[source] != -1)
                  image[source] = std::min (int (image[source]),
                                            std::max (-1, int (value[destination]) - increment));
              return vector_type (std::span<const typename vector_type::value_type> (image));
            }));
          });
          if (first) {
            combined = std::move (mapped);
            first = false;
          }
          else
            timed ("union", [&] { combined->union_with (std::move (*mapped)); });
        }
        timed ("intersection", [&] { frontier->intersect_with (std::move (*combined)); });
      }
      if (not verify_only) {
        if (not detail)
          CALLGRIND_STOP_INSTRUMENTATION;
        const auto wall_end = std::chrono::steady_clock::now ();
        const auto cpu_end = std::clock ();
        result["cpre"] += std::chrono::duration<double> (wall_end - start).count ();
        result["cpre_cpu"] += double (cpu_end - cpu) / CLOCKS_PER_SEC;
      }
      std::set<logical> actual;
      if (not frontier)
        throw std::runtime_error ("missing replay frontier");
      const auto& completed = frontier.value ();
      for (const auto& value : completed)
        require (actual.insert (matrix_check::unpack (value)).second, "duplicate replay maximum");
      require (actual == expected and completed.size () == expected.size (),
               "backend CPre result disagrees with source");
      logical initial (input.dimension, -1);
      initial[input.initial] = 0;
      bool answer = false;
      timed ("contains",
             [&] { answer = completed.contains (matrix_check::vector<vector_type> (initial)); });
      require (answer == std::ranges::any_of (
                             input.after,
                             [&] (const auto& v) { return matrix_check::below (initial, v); }),
               "replayed initial-state membership disagrees with scalar oracle");
      std::optional<S> rebuilt;
      timed ("rebuild", [&] { rebuilt.emplace (matrix_check::build<S> (input.after)); });
    }
    if (not verify_only) {
      result["rounds"] = double (rounds);
      result["detail"] = detail;
      result["dimension"] = double (input.dimension);
      result["boolean_tail"] = double (input.dimension - input.numeric);
      result["before_maxima"] = double (input.before.size ());
      result["after_maxima"] = double (input.after.size ());
      result["actions"] = double (input.actions.size ());
    }
    return result;
  }
}
