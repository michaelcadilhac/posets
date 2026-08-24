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

antichain_factory_test_exe = executable ('antichain-factory-test',
                                         'antichain-factory-test.cc',
                                         dependencies : [posets_dep ])

empty_downsets_test_exe = executable ('empty-downsets-test',
                                      'empty-downsets.cc',
                                      dependencies : [posets_dep ])

test('antichains/vectors random', downsetbm_exe,
     args : ['all', 'all',
             '--params=build=5,query=5,transfer=5,intersection=5,union=5,cpre=5,cpre_actions=2' ])
test('antichains/vectors random d128', downsetbm_d128_exe,
     args : ['all', 'all',
             '--params=build=5,query=5,transfer=5,intersection=5,union=5,cpre=5,cpre_actions=2,bitset_tail=32' ])
test('antichains/vectors simd benchmark', simdbm_exe,
     args : ['vector_backed', 'simd_array_backed_sum_fixed<test_value_type>'])
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
test('antichains/vector backed unchecked factory', antichain_factory_test_exe)
test('antichains/empty downsets', empty_downsets_test_exe)
