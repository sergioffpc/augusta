#ifndef AUGUSTA_TESTS_ALLOCATOR_CANDIDATE_H_
#define AUGUSTA_TESTS_ALLOCATOR_CANDIDATE_H_

#include <string_view>

// The allocator one allocator benchmark executable measures (#1351): each
// augusta_allocator_benchmarks_<candidate> links one allocator_<candidate>.cpp,
// which defines these, beside allocator_benchmark_main.cpp, which checks the
// candidate before it runs the benchmarks. How the candidate replaces
// malloc/free and new/delete for the whole process is its executable's link
// (tests/CMakeLists.txt), not this file's.
namespace augusta::benchmarks {

// The candidate's name, as its executable's ends, which its results' context
// names it by.
extern const std::string_view kCandidate;

// Whether the candidate served `block`, which malloc or operator new has just
// handed out: false when the platform's allocator still does.
bool CandidateServed(void* block);

}  // namespace augusta::benchmarks

#endif  // AUGUSTA_TESTS_ALLOCATOR_CANDIDATE_H_
