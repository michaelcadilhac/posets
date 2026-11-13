tests_exe = executable ('tests', 'tests.cc',
                        dependencies : [posets_dep ],
                        cpp_args : '-O0' )  # Deactivate optimization, otherwise it takes ages.

kdtests_exe = executable ('kdtests', 'kdtests.cc',
                          dependencies : [posets_dep ])

sttests_exe = executable ('sttests', 'sttests.cc',
                          dependencies : [posets_dep ])

csttests_exe = executable ('csttests', 'csttests.cc',
                           dependencies : [posets_dep ])

cst_arithmetic_exe = executable ('cst-arithmetic', 'cst-arithmetic.cc',
                                dependencies : [posets_dep ])

simdbm_exe = executable ('simd-bm', 'simd-bm.cc',
                         dependencies : [posets_dep ])

# Exercise the real benchmark kernel without a quadratic 16K-item workload
# in debug/Memcheck. The standalone benchmark retains its full default size.
simdbm_smoke_exe = executable('simd-bm-smoke', 'simd-bm.cc',
  dependencies: [posets_dep], cpp_args: ['-DPOSETS_SIMD_BM_ITEMS=256'])

downsetbm_exe = executable ('downset-bm', 'downset-bm.cc',
                            dependencies : [posets_dep ])

downsetbm_d128_exe = executable ('downset-bm-d128', 'downset-bm.cc',
                                 dependencies : [posets_dep ],
                                 cpp_args : ['-UDIMENSION', '-DDIMENSION=128'])

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

# Benchmark kernels warn on cross-backend size differences; these are CLI smoke.
# Independent semantic coverage lives in the matrix/* oracle tests below.
test('benchmarks/downsets smoke', downsetbm_exe,
     args : ['all', 'all',
             '--params=build=5,query=5,transfer=5,intersection=5,union=5,cpre=5,cpre_actions=2' ])
test('benchmarks/downsets smoke d128', downsetbm_d128_exe,
     args : ['all', 'all',
             '--params=build=5,query=5,transfer=5,intersection=5,union=5,cpre=5,cpre_actions=2,bitset_tail=32' ])
test('benchmarks/SIMD kernel smoke', simdbm_smoke_exe,
     args : ['vector_backed', 'simd_array_backed_sum_fixed<test_value_type>'])
test('benchmarks/SIMD CLI validation', find_program('python3'),
     args: [meson.current_source_dir() / 'cli-check.py', simdbm_exe])
test('antichains/vectors implementations', tests_exe, args : ['all', 'all'])
test('antichains/vectors kdtests', kdtests_exe)
test('antichains/vectors sttests', sttests_exe)
test('antichains/vectors csttests', csttests_exe)
foreach mode : ['prefix', 'arity', 'narrowing', 'extremes', 'oracle', 'domain']
  test('antichains/CST arithmetic/' + mode, cst_arithmetic_exe, args: [mode])
endforeach
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

# Keep the 24-backend matrix on the two dynamic vector representations. Synthetic
# timing still uses downset-bm; --check runs an independent logical scalar oracle.
# GCC on Darwin/arm64 does not accept -march=native. Keep that flag on x86,
# where it exercises wider registers; other targets use their compiler default.
matrix_target_args = []
if get_option('test_native_isa')
  if host_machine.cpu_family() in ['x86', 'x86_64']
    matrix_target_args = ['-march=native']
  else
    message('SIMD matrix uses the default compilation target (no extra ISA flags)')
  endif
endif

foreach mode : ['compatible', 'native', 'max']
  matrix_args = ['-DPOSETS_MATRIX_ONLY',
                 '-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper()] + matrix_target_args
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
       args: [meson.current_source_dir() / 'matrix-trace-check.py', matrix_exe], timeout: 120)
  tail_exe = executable('simd-tail-' + mode, 'simd-tail-check.cc',
                       dependencies: [posets_dep], cpp_args: matrix_args)
  test('matrix/packed Boolean tails/' + mode, tail_exe)
  width_exe = executable('simd-width-' + mode, 'simd-width-check.cc',
    dependencies: [posets_dep], cpp_args: matrix_args +
      ['-DPOSETS_EXPECT_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper()])
  test('matrix/SIMD width and storage/' + mode, width_exe)
  order_exe = executable('vector-order-' + mode, 'vector-order-check.cc',
    dependencies: [posets_dep], cpp_args: matrix_args)
  test('matrix/full-range signed order/' + mode, order_exe)
  sharing_antichain_exe = executable('sharing-antichain-' + mode, 'sharing-antichain.cc',
    dependencies: [posets_dep], cpp_args: matrix_args)
  test('antichains/sharing backends/' + mode, sharing_antichain_exe)
endforeach

selected_kd_matrix_exe = executable('downset-matrix-kd-selected', 'downset-bm.cc',
  dependencies: [posets_dep], cpp_args: ['-DPOSETS_MATRIX_ONLY',
    '-DPOSETS_MATRIX_BACKEND=posets::downsets::kdtree_backed',
    '-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_COMPATIBLE'] + matrix_target_args)
foreach vector : ['vector_backed', 'simd_vector_backed']
  test('matrix/single-backend registration/' + vector, selected_kd_matrix_exe,
    args: ['--check=nonempty', 'kdtree_backed', vector + '<test_value_type>'])
endforeach

# Exercise defaults and every legacy setting independently of the new selector.
legacy_modes = [
  ['default', [], 'NATIVE'],
  ['legacy-max-off', ['-DSIMD_IS_MAX=0'], 'NATIVE'],
  ['legacy-max-false', ['-DSIMD_IS_MAX=false'], 'NATIVE'],
  ['legacy-native-off', ['-DSIMD_IS_NATIVE=0'], 'COMPATIBLE'],
  ['legacy-both-off', ['-DSIMD_IS_NATIVE=0', '-DSIMD_IS_MAX=0'], 'COMPATIBLE'],
  ['legacy-native', ['-DSIMD_IS_NATIVE=true'], 'NATIVE'],
  ['native-overrides-legacy-max', ['-DSIMD_IS_NATIVE=1', '-DSIMD_IS_MAX=1'], 'NATIVE'],
  ['legacy-max', ['-DSIMD_IS_MAX=1'], 'MAX'],
  ['legacy-max-native-off', ['-DSIMD_IS_NATIVE=0', '-DSIMD_IS_MAX=1'], 'MAX'],
  ['explicit-overrides-legacy', ['-DPOSETS_SIMD_WIDTH=0', '-DSIMD_IS_NATIVE=1',
                               '-DSIMD_IS_MAX=1'], 'COMPATIBLE'],
]
foreach mode : legacy_modes
  mode_args = mode[1] + ['-DPOSETS_EXPECT_WIDTH=POSETS_SIMD_WIDTH_' + mode[2]] + matrix_target_args
  selector_exe = executable('simd-selector-' + mode[0], 'simd-selector-check.cc',
    dependencies: [posets_dep], cpp_args: mode_args)
  test('matrix/SIMD selector/' + mode[0], selector_exe)
endforeach

# Max mode forces over-aligned SIMD allocation even on the portable SSE target.
sharingtrie_allocation_exe = executable('sharingtrie-allocation',
  'sharingtrie-allocation.cc', dependencies: [posets_dep],
  cpp_args: ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_MAX'])
test('antichains/sharingtrie allocation failures', sharingtrie_allocation_exe,
  env: sharing_allocation_env, timeout: 120)

block_allocation_exe = executable('block-sharingtrie-allocation',
  'sharingtrie-allocation.cc', dependencies: [posets_dep],
  cpp_args: ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_MAX', '-DPOSETS_TEST_BLOCK_SHARINGTRIE'])
test('antichains/block sharingtrie allocation failures', block_allocation_exe,
  env: sharing_allocation_env, timeout: 120)
foreach mode : ['compatible', 'native', 'max']
  block_exe = executable('block-sharingtrie-' + mode, 'block-sharingtrie.cc',
    dependencies: [posets_dep], cpp_args:
      ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper()] + matrix_target_args)
  test('antichains/block sharingtrie/' + mode, block_exe, timeout: 120)
endforeach

layout_controls = [
  ['baseline', []],
  ['trie-compact', ['-DPOSETS_SHARINGTRIE_COMPACT_NODES=1']],
  ['kd-narrow', ['-DPOSETS_KDTREE_NARROW_INDICES=1']],
  ['comp-simd', ['-DPOSETS_COMPTRIE_SIMD_PATHS=1']],
]
foreach control : layout_controls
  layout_exe = executable('layout-check-' + control[0], 'layout-check.cc',
    dependencies: [posets_dep], cpp_args: control[1] + matrix_target_args)
  test('layout/oracle and accounting/' + control[0], layout_exe)
  layout_bm = executable('layout-bm-' + control[0], 'layout-bm.cc',
    dependencies: [posets_dep], cpp_args: control[1] + matrix_target_args)
  layout_memory = executable('layout-memory-' + control[0], 'layout-bm.cc',
    dependencies: [posets_dep], cpp_args: control[1] + matrix_target_args + ['-DPOSETS_TRACK_ALLOCATIONS=1'])
  test('layout/memory and shape oracles/' + control[0], find_program('python3'),
    args: [meson.current_source_dir() / 'layout-smoke.py', layout_memory],
    env: sharing_allocation_env, timeout: 120)
endforeach
compact_allocation_exe = executable('sharingtrie-compact-allocation',
  'sharingtrie-allocation.cc', dependencies: [posets_dep],
  cpp_args: ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_MAX', '-DPOSETS_SHARINGTRIE_COMPACT_NODES=1'])
test('layout/sharingtrie compact allocation failures', compact_allocation_exe,
  env: sharing_allocation_env, timeout: 120)

foreach mode : ['compatible', 'native', 'max']
  lazy_kd_exe = executable('lazy-kdtree-' + mode, 'lazy-kdtree.cc',
    dependencies: [posets_dep], cpp_args:
      ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper(), '-DPOSETS_LAZY_KDTREE_STATS=1'] + matrix_target_args)
  test('antichains/lazy KD oracle and FC/' + mode, lazy_kd_exe, timeout: 120)
endforeach
lazy_allocation_exe = executable('lazy-kdtree-allocation', 'lazy-kdtree-allocation.cc',
  dependencies: [posets_dep], cpp_args: ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_MAX'])
test('antichains/lazy KD allocation failures', lazy_allocation_exe,
  env: sharing_allocation_env, timeout: 120)
lazy_policy_exe = executable('lazy-kdtree-policy', 'lazy-kdtree-policy.cc',
  dependencies: [posets_dep], cpp_args: ['-Ofast', '-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_COMPATIBLE'])
test('antichains/lazy KD policy fast math', lazy_policy_exe)

forest_kd_layout_exe = executable('forest-kd-layout', 'forest-kd-layout.cc',
  dependencies: [posets_dep])
test('layout/forest cache and KD capacity', forest_kd_layout_exe,
  env: sharing_allocation_env, timeout: 120)

foreach mode : ['baseline', 'narrow']
  kd_scratch_args = mode == 'narrow' ? ['-DPOSETS_KDTREE_NARROW_INDICES=1'] : []
  kd_scratch_exe = executable('kdtree-scratch-' + mode, 'kdtree-scratch.cc',
    dependencies: [posets_dep], cpp_args: kd_scratch_args)
  test('layout/KD scratch restoration/' + mode, kd_scratch_exe)
endforeach

foreach mode : ['compatible', 'native', 'max']
  ranked_default_exe = executable('rank-bucketed-' + mode, 'rank-bucketed.cc',
    dependencies: [posets_dep], cpp_args:
      ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper(), '-DPOSETS_RANK_STATS'] + matrix_target_args)
  test('antichains/ranked default oracle and failures/' + mode, ranked_default_exe,
    env: sharing_allocation_env, timeout: 120)
  signed_domains_exe = executable('signed-domains-' + mode, 'signed-domains.cc',
    dependencies: [posets_dep], cpp_args:
      ['-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_' + mode.to_upper()] + matrix_target_args)
  test('antichains/signed bins and exact sums/' + mode, signed_domains_exe, timeout: 120)
endforeach

# Check the declared install list against the source tree and compile each header
# without an umbrella include. Staged compilation gets only installed paths.
cpp = meson.get_compiler('cpp')
test('headers/source consumption', find_program('python3'),
  suite: 'packaging',
  args: [meson.current_source_dir() / 'header-check.py',
         '--include-dir', meson.project_source_root() / 'include',
         '--boost-dir', meson.project_source_root() / 'lib',
         '--reference-dir', meson.project_source_root() / 'include',
         '--compiler'] + cpp.cmd_array(), timeout: 180)
test('headers/staged install', find_program('python3'),
  suite: 'packaging',
  args: [meson.current_source_dir() / 'install-smoke.py',
         meson.project_build_root()] + cpp.cmd_array(), timeout: 180)
