"""Unexecuted schema checks; complete synthetic stdout, no spec/profile generation."""
import unittest

from project_profiles import accept_stdout


class ContentChecks(unittest.TestCase):
    def encode(self, fields):
        return ' '.join(f'{key}={value}' for key, value in fields.items())

    def decode(self, line):
        return dict(word.split('=', 1) for word in line.split())

    def fixture(self):
        metadata = {'instrumented': 1, 'clocks_discarded': 1, 'vector': 'simd_vector_backed',
                    'mode': 'roi', 'dimension': 2, 'numeric': 1, 'before': 2, 'actions': 1,
                    'selected_lanes': 16, 'width_selector': 0, 'element_bytes': 1}
        sequence = ['build_normalized', 'cpre_after_rhs_destruction_net', 'contains',
                    'rebuild_normalized_first_alive']
        layout = {'vector_bytes': 16, 'vector_alignment': 8,
                  'move_construct_noexcept': 1, 'move_assign_noexcept': 1}
        # Independent explicit inventory of all42 emitted phase fields.
        phase = {
            'phase': '', 'success': 1, 'has_result': 0, 'size': 7, 'generator_capacity': 9,
            'entry_live': 100, 'exit_live': 83, 'live_delta': -17, 'extra_peak': 30,
            'allocations': 2, 'requested_bytes': 40,
            'rank_started': 2, 'rank_completed': 2,
            'rank_active_coordinates_planned': 4, 'rank_active_coordinates_completed': 4,
            'vector_copy_attempts': 0, 'vector_move_constructions_completed': 0,
            'vector_move_assignments_completed': 0, 'vector_base_adoptions_completed': 0,
            'vector_partial_order_attempts': 0, 'vector_meet_attempts': 0,
            'intersections_started': 0, 'intersections_completed': 0,
            'intersections_noop': 0, 'intersections_changed': 0,
            'last_intersection_left': 0, 'last_intersection_queries': 0,
            'last_intersection_reason': 'not_called', 'last_intersection_first_failed_query': 'null',
            'rank_contains_calls': 0, 'rank_partial_order_calls': 0, 'rank_insert_attempts': 0,
            'rank_insert_rejected_dominated': 0, 'rank_existing_elements_removed': 0,
            'rank_union_insertions': 0, 'rank_intersection_left_elements': 0,
            'rank_intersection_short_circuits': 0, 'rank_intersection_pair_meets': 0,
            'rank_apply_elements': 0, 'rank_peak_backing_size': 0,
            'rank_bucket_rebuilds': 0, 'rank_bucket_rebuild_elements': 0,
        }
        lines = [self.encode(metadata), self.encode(layout)]
        for name in sequence:
            lines.append(self.encode({**phase, 'phase': name}))
        lines.append('kernel_check=pass after_kernel_cleanup_live=37 cleanup_live_delta=11')
        return lines, {'metadata': metadata, 'roi_sequence': sequence}

    def accept(self, lines, expected):
        return accept_stdout('\n'.join(lines) + '\n', expected)

    def changed(self, original, row, key, value):
        lines = list(original)
        fields = self.decode(lines[row])
        fields[key] = value
        lines[row] = self.encode(fields)
        return lines

    def test_complete_schema_and_nonzero_deltas(self):
        lines, expected = self.fixture()
        parsed = self.accept(lines, expected)
        self.assertIsNone(parsed['phases'][0]['size'])
        self.assertIsNone(parsed['phases'][0]['generator_capacity'])
        self.assertIsNone(parsed['phases'][0]['last_intersection_first_failed_query'])
        self.assertEqual(parsed['phases'][0]['live_delta'], '-17')
        self.assertEqual(parsed['cleanup']['cleanup_live_delta'], '11')
        lines = self.changed(lines, 2, 'has_result', '1')
        self.assertEqual(self.accept(lines, expected)['phases'][0]['size'], '7')

    def test_every_emitted_key_required(self):
        original, expected = self.fixture()
        for index in (0, 1, 2, len(original) - 1):
            for key in self.decode(original[index]):
                lines = list(original)
                fields = self.decode(lines[index])
                fields.pop(key)
                lines[index] = self.encode(fields)
                with self.subTest(row=index, missing=key), self.assertRaises(ValueError):
                    self.accept(lines, expected)

    def test_unsigned_fields_reject_noninteger_negative_and_overflow(self):
        original, expected = self.fixture()
        non_unsigned = {'phase', 'success', 'has_result', 'live_delta',
                        'last_intersection_reason', 'last_intersection_first_failed_query'}
        phase_keys = set(self.decode(original[2])) - non_unsigned
        groups = [(2, phase_keys), (1, {'vector_bytes', 'vector_alignment'}),
                  (len(original) - 1, {'after_kernel_cleanup_live'})]
        for index, keys in groups:
            for key in keys:
                for value in ('nan', '-1', '18446744073709551616'):
                    with self.subTest(row=index, key=key, value=value), self.assertRaises(ValueError):
                        self.accept(self.changed(original, index, key, value), expected)
        for value in ('1.5', '1e3', '+1', '01', '', '\u0661'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.accept(self.changed(original, 2, 'vector_copy_attempts', value), expected)
        for key in ('vector_bytes', 'vector_alignment'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.accept(self.changed(original, 1, key, '0'), expected)

    def test_signed_deltas_and_null_or_index(self):
        original, expected = self.fixture()
        for index, key in ((2, 'live_delta'), (len(original) - 1, 'cleanup_live_delta')):
            for value in ('-9223372036854775808', '9223372036854775807', '-3', '8', '0'):
                self.accept(self.changed(original, index, key, value), expected)
            for value in ('-9223372036854775809', '9223372036854775808', 'nan', '1.5', '-0', '+1'):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.accept(self.changed(original, index, key, value), expected)
        for value in ('null', '0', '18446744073709551615'):
            self.accept(self.changed(original, 2, 'last_intersection_first_failed_query', value), expected)
        for value in ('NULL', '-1', '1.5', '18446744073709551616'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.accept(self.changed(original, 2, 'last_intersection_first_failed_query', value), expected)
        self.accept(self.changed(original, 2, 'rank_active_coordinates_completed', '18446744073709551615'), expected)

    def test_text_flags_duplicate_extra_and_roi_integrity(self):
        original, expected = self.fixture()
        cases = [(0, 'mode', 'unknown'), (0, 'numeric', 'NaN'), (0, 'width_selector', '1.0'),
                 (1, 'move_construct_noexcept', 'true'), (2, 'phase', 'unknown'),
                 (2, 'success', '0'), (2, 'has_result', '2'),
                 (2, 'last_intersection_reason', 'unknown'), (len(original) - 1, 'kernel_check', 'fail')]
        for index, key, value in cases:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.accept(self.changed(original, index, key, value), expected)
        for suffix in (' rank_completed=2', ' unknown_counter=0'):
            lines = list(original)
            lines[2] += suffix
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                self.accept(lines, expected)
        lines = list(original)
        lines.pop(3)
        with self.assertRaises(ValueError):
            self.accept(lines, expected)

    def test_operations_keeps_only_existing_phase_name_policy(self):
        original, expected = self.fixture()
        expected['metadata']['mode'] = 'operations'
        expected['roi_sequence'] = None
        # One synthetic phase proves the parser adds no operation-count/order rule.
        for name in ('constructor_only_caller_alive', 'contains', 'apply',
                     'union_rhs_still_alive', 'intersection_rhs_still_alive'):
            row = self.decode(original[2])
            row['phase'] = name
            lines = [self.encode(expected['metadata']), original[1], self.encode(row), original[-1]]
            self.accept(lines, expected)
