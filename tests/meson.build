tests_exe = executable ('tests', 'tests.cc',
                        dependencies : [posets_dep ],
                        c_args : '-O0' )  # Deactivate optimization, otherwise it takes ages.

kdtests_exe = executable ('kdtests', 'kdtests.cc',
                          dependencies : [posets_dep ])

sttests_exe = executable ('sttests', 'sttests.cc',
                          dependencies : [posets_dep ])

csttests_exe = executable ('csttests', 'csttests.cc',
                           dependencies : [posets_dep ])

simdbm_exe = executable ('simd-bm', 'simd-bm.cc',
                         dependencies : [posets_dep ])

downsetbm_exe = executable ('downset-bm', 'downset-bm.cc',
                            dependencies : [posets_dep ])

downsetbm_d128_exe = executable ('downset-bm-d128', 'downset-bm.cc',
                                 dependencies : [posets_dep ],
                                 cpp_args : '-DDIMENSION=128')

reducetests_exe = executable ('reducetests', 'reducetests.cc',
                              dependencies : [posets_dep ])

bboxtests_exe = executable ('bboxtests', 'bboxtests.cc',
                            dependencies : [posets_dep ])

comptrietests_exe = executable ('comptrietests', 'comptrietests.cc',
                                dependencies : [posets_dep ])

lcrstests_exe = executable ('lcrstests', 'lcrstests.cc',
                            dependencies : [posets_dep ])

concept_checks_exe = executable ('concept-checks', 'concept-checks.cc',
                                 dependencies : [posets_dep ])

pruned_concept_checks_exe = executable ('pruned-concept-checks', 'pruned-concept-checks.cc',
                                        dependencies : [posets_dep ])

pruned_vector_only_concept_checks_exe = executable (
  'pruned-vector-only-concept-checks',
  'pruned-vector-only-concept-checks.cc',
  dependencies : [posets_dep ])

antichain_factory_test_exe = executable ('antichain-factory-test',
                                         'antichain-factory-test.cc',
                                         dependencies : [posets_dep ])

rank_stats_on_exe = executable ('rank-stats-on',
                                'rank-stats.cc',
                                dependencies : [posets_dep ],
                                cpp_args : '-DPOSETS_RANK_STATS')

rank_stats_off_exe = executable ('rank-stats-off',
                                 'rank-stats.cc',
                                 dependencies : [posets_dep ])

empty_downsets_test_exe = executable ('empty-downsets-test',
                                      'empty-downsets.cc',
                                      dependencies : [posets_dep ])

vector_move_exe = executable ('vector-move', 'vector-move.cc',
                              dependencies : [posets_dep ])

test('antichains/vectors random', downsetbm_exe,
     args : ['all', 'all',
             '--params=build=5,query=5,transfer=5,intersection=5,union=5,cpre=5,cpre_actions=2' ])
test('antichains/vectors random d128', downsetbm_d128_exe,
     args : ['all', 'all',
             '--params=build=5,query=5,transfer=5,intersection=5,union=5,cpre=5,cpre_actions=2,bitset_tail=32' ])
test('antichains/vectors simd benchmark', simdbm_exe,
     args : ['vector_backed', 'simd_array_backed_sum_fixed<test_value_type>'])
test('benchmarks/reject unknown SIMD implementation', simdbm_exe,
     args: ['vector_backed', 'unknown'], should_fail: true)
test('antichains/vectors implementations', tests_exe, args : ['all', 'all'])
test('antichains/vectors kdtests', kdtests_exe)
test('antichains/vectors sttests', sttests_exe)
test('antichains/vectors csttests', csttests_exe)
test('antichains/vectors lcrstests', lcrstests_exe)
test('antichains/vectors reduce', reducetests_exe)
test('antichains/vectors bboxtree', bboxtests_exe)
test('antichains/vectors comptrie', comptrietests_exe)
test('antichains/vectors concept checks', concept_checks_exe)
test('antichains/vectors pruned concept checks', pruned_concept_checks_exe)
test('antichains/vectors pruned vector-only concept checks',
     pruned_vector_only_concept_checks_exe)
test('antichains/vector backed unchecked factory', antichain_factory_test_exe)
test('antichains/empty downsets', empty_downsets_test_exe)
test('vectors/move assignment', vector_move_exe)
test('antichains/rank stats enabled', rank_stats_on_exe)
test('antichains/rank stats compiled out', rank_stats_off_exe)

sharing_allocation_exe = executable('sharing-antichain-allocation',
  'sharing-antichain-allocation.cc', dependencies: [posets_dep])
# Keep fixture hooks active; Memcheck still tracks their underlying libc allocations.
sharing_allocation_env = environment()
sharing_allocation_env.append('VALGRIND_OPTS',
  '--soname-synonyms=somalloc=nouserintercepts', separator: ' ')
test('antichains/sharing allocation failures', sharing_allocation_exe, env: sharing_allocation_env)

# Keep the 14-backend matrix on the two dynamic vector representations. Synthetic
# timing still uses downset-bm; --check runs an independent logical scalar oracle.
foreach mode : ['compatible', 'native', 'max']
  matrix_args = ['-DPOSETS_MATRIX_ONLY',
                 '-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper()]
  if get_option('test_native_isa')
    matrix_args += ['-march=native']
  endif
  matrix_exe = executable('downset-matrix-' + mode, 'downset-bm.cc',
                          dependencies: [posets_dep], cpp_args: matrix_args)
  test('matrix/vector semantics/' + mode, matrix_exe,
       args: ['--check=vectors', 'vector_backed', 'all'])
  test('matrix/CST empty/' + mode, matrix_exe,
       args: ['--check=empty', 'cst_backed', 'all'])
  test('matrix/BBox empty/' + mode, matrix_exe,
       args: ['--check=empty', 'bboxtree_backed', 'all'])
  test('matrix/BBox signed algebra/' + mode, matrix_exe,
       args: ['--check=nonempty', 'bboxtree_backed', 'all'])
  test('matrix/recorded operations/' + mode, find_program('python3'),
       args: [meson.current_source_dir() / 'matrix-trace-check.py', matrix_exe], timeout: 60)
  tail_exe = executable('simd-tail-' + mode, 'simd-tail-check.cc',
                       dependencies: [posets_dep], cpp_args: matrix_args)
  test('matrix/packed Boolean tails/' + mode, tail_exe)
  width_exe = executable('simd-width-' + mode, 'simd-width-check.cc',
    dependencies: [posets_dep], cpp_args: matrix_args +
      ['-DPOSETS_EXPECT_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper()])
  test('matrix/SIMD width and storage/' + mode, width_exe)
  sharing_antichain_exe = executable('sharing-antichain-' + mode, 'sharing-antichain.cc',
    dependencies: [posets_dep], cpp_args: matrix_args)
  test('antichains/sharing backends/' + mode, sharing_antichain_exe)
endforeach

native_precedence_exe = executable('simd-native-precedence', 'simd-native-mode.cc',
  dependencies: [posets_dep], cpp_args: ['-DSIMD_IS_NATIVE=1', '-DSIMD_IS_MAX=1'])
test('matrix/native overrides legacy max', native_precedence_exe)

# Exercise defaults and every legacy setting independently of the new selector.
legacy_modes = [
  ['default', [], 'NATIVE'],
  ['legacy-max-off', ['-DSIMD_IS_MAX=0'], 'NATIVE'],
  ['legacy-max-false', ['-DSIMD_IS_MAX=false'], 'NATIVE'],
  ['legacy-native-off', ['-DSIMD_IS_NATIVE=0'], 'COMPATIBLE'],
  ['legacy-both-off', ['-DSIMD_IS_NATIVE=0', '-DSIMD_IS_MAX=0'], 'COMPATIBLE'],
  ['legacy-native', ['-DSIMD_IS_NATIVE=true'], 'NATIVE'],
  ['legacy-max', ['-DSIMD_IS_MAX=1'], 'MAX'],
  ['legacy-max-native-off', ['-DSIMD_IS_NATIVE=0', '-DSIMD_IS_MAX=1'], 'MAX'],
  ['explicit-overrides-legacy', ['-DPOSETS_SIMD_WIDTH=0', '-DSIMD_IS_NATIVE=1',
                               '-DSIMD_IS_MAX=1'], 'COMPATIBLE'],
]
foreach mode : legacy_modes
  mode_args = mode[1] + ['-DPOSETS_EXPECT_WIDTH=POSETS_SIMD_WIDTH_' + mode[2]]
  if get_option('test_native_isa')
    mode_args += ['-march=native']
  endif
  width_exe = executable('simd-width-' + mode[0], 'simd-width-check.cc',
    dependencies: [posets_dep], cpp_args: mode_args)
  test('matrix/SIMD selector/' + mode[0], width_exe)
endforeach

# Max mode forces over-aligned SIMD allocation even on the portable SSE target.
sharingtrie_allocation_exe = executable('sharingtrie-allocation',
  'sharingtrie-allocation.cc', dependencies: [posets_dep],
  cpp_args: ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_MAX'])
test('antichains/sharingtrie allocation failures', sharingtrie_allocation_exe,
  env: sharing_allocation_env, timeout: 120)
