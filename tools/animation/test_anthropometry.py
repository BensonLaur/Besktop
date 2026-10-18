"""Small offline regressions for source units, endpoint semantics and profile drift."""
import copy
import math
from pathlib import Path
import tempfile
import unittest

from anthropometry import audit_profile, load, segment_lengths, verify_osim


class AnthropometryTests(unittest.TestCase):
    def setUp(self):
        directory = Path(__file__).resolve().parent
        self.reference = load(directory / "anthropometry_reference.json")
        self.profile = load(directory / "rig_profile.json")

    def test_known_neutral_spans(self):
        lengths = segment_lengths(self.reference)
        self.assertAlmostEqual(lengths["thigh_r"], .408049472735844, places=12)
        self.assertAlmostEqual(lengths["shin_r"], .396472583063847, places=12)
        self.assertEqual(lengths["thigh_l"], lengths["thigh_r"])
        self.assertEqual(lengths["shin_l"], lengths["shin_r"])

    def test_derived_profile(self):
        audit = audit_profile(self.reference, self.profile)
        self.assertAlmostEqual(audit["source_thigh_shin_ratio"], 1.02919972317514, places=12)
        self.assertAlmostEqual(sum(audit["derived_profile_m"].values()), .87, places=12)
        self.assertTrue(audit["not_population_average"])

    def test_reject_units(self):
        self.reference["units"] = "millimeters"
        with self.assertRaises(ValueError):
            audit_profile(self.reference, self.profile)

    def test_reject_nonfinite_endpoint(self):
        for invalid in (math.nan, math.inf):
            reference = copy.deepcopy(self.reference)
            reference["segments"]["shin_l"]["distal"][0] = invalid
            with self.assertRaises(ValueError):
                audit_profile(reference, self.profile)

    def test_reject_wrong_endpoint_size(self):
        self.reference["segments"]["shin_l"]["proximal"] = [0, 0]
        with self.assertRaises(ValueError):
            audit_profile(self.reference, self.profile)

    def test_reject_profile_drift(self):
        self.profile["thigh_length"] += .001
        with self.assertRaises(ValueError):
            audit_profile(self.reference, self.profile)

    def test_reject_version_mismatch(self):
        self.profile["joint_reference"] = "unknown"
        with self.assertRaises(ValueError):
            audit_profile(self.reference, self.profile)

    def test_reject_nonfinite_target(self):
        self.reference["adaptation"]["target_leg_span_m"] = math.nan
        with self.assertRaises(ValueError):
            audit_profile(self.reference, self.profile)

    def test_reject_source_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.osim"
            path.write_bytes(b"not the pinned model")
            with self.assertRaisesRegex(ValueError, "checksum"):
                verify_osim(self.reference, path)


if __name__ == "__main__":
    unittest.main()
