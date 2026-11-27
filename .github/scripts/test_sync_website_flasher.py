"""
Unit tests for sync_website_flasher.py
Tests:
  - Semantic version parsing
  - Retention rules (patch caps, minor switches, major switches, pre-release top-5)
  - Multi-device isolation
  - End-to-end sync, manifest generation, and directory pruning
"""

import json
import shutil
import tempfile
import unittest
from pathlib import Path

import sync_website_flasher


class TestSyncWebsiteFlasher(unittest.TestCase):
    def test_01_parse_semver(self):
        # Stable
        p_stable = sync_website_flasher.parse_semver("v1.2.3")
        self.assertEqual(p_stable, (1, 2, 3, 1, 0, 0))

        # Pre-release
        p_rc = sync_website_flasher.parse_semver("v1.2.3-rc.20261122.2")
        self.assertEqual(p_rc, (1, 2, 3, 0, 20261122, 2))

        # Stable is greater than RC of the same M.m.p
        self.assertGreater(p_stable, p_rc)

    def test_02_retention_single_minor_patches(self):
        """Within current minor, keep at most the last 2 patch versions."""
        tags = ["v0.1.0", "v0.1.1", "v0.1.2"]
        retained = sync_website_flasher.determine_retained_versions(tags)
        self.assertEqual(retained, {"v0.1.2", "v0.1.1"})
        self.assertNotIn("v0.1.0", retained)

    def test_03_retention_minor_switches(self):
        """Keep last 2 minor versions (2 patches of current, last stable of prior)."""
        tags = [
            "v0.1.0", "v0.1.1",           # Old minor (pruned)
            "v0.2.0", "v0.2.1",           # Prior minor (keep v0.2.1 only)
            "v0.3.0", "v0.3.1", "v0.3.2", # Current minor (keep v0.3.2, v0.3.1)
        ]
        retained = sync_website_flasher.determine_retained_versions(tags)
        self.assertEqual(retained, {"v0.3.2", "v0.3.1", "v0.2.1"})
        self.assertNotIn("v0.3.0", retained)
        self.assertNotIn("v0.2.0", retained)
        self.assertNotIn("v0.1.1", retained)
        self.assertNotIn("v0.1.0", retained)

    def test_04_retention_major_switches(self):
        """When switching major version, keep only last stable of (current_major - 1)."""
        tags = [
            "v0.3.2",                     # Major 0 (pruned when Major 2 exists)
            "v1.0.0", "v1.1.0", "v1.1.1", # Major 1 (keep v1.1.1 only)
            "v2.0.0",                     # Current Major 2 (keep)
        ]
        retained = sync_website_flasher.determine_retained_versions(tags)
        self.assertEqual(retained, {"v2.0.0", "v1.1.1"})
        self.assertNotIn("v0.3.2", retained)
        self.assertNotIn("v1.1.0", retained)
        self.assertNotIn("v1.0.0", retained)

    def test_05_retention_prereleases(self):
        """Always keep the last 5 pre-releases regardless of M.m.p."""
        tags = [
            "v0.1.0-rc.20261101.1",  # Pruned (oldest)
            "v0.1.0-rc.20261102.1",  # Pruned
            "v0.1.0-rc.20261103.1",  # Kept
            "v0.1.0-rc.20261104.1",  # Kept
            "v0.2.0-rc.20261105.1",  # Kept
            "v0.2.0-rc.20261106.1",  # Kept
            "v0.2.0-rc.20261106.2",  # Kept (newest)
        ]
        retained = sync_website_flasher.determine_retained_versions(tags)
        self.assertEqual(len(retained), 5)
        self.assertIn("v0.2.0-rc.20261106.2", retained)
        self.assertIn("v0.2.0-rc.20261106.1", retained)
        self.assertIn("v0.2.0-rc.20261105.1", retained)
        self.assertIn("v0.1.0-rc.20261104.1", retained)
        self.assertIn("v0.1.0-rc.20261103.1", retained)
        self.assertNotIn("v0.1.0-rc.20261102.1", retained)
        self.assertNotIn("v0.1.0-rc.20261101.1", retained)

    def test_06_end_to_end_mock_sync_and_directory_pruning(self):
        """Verifies binary copying, manifest writing, and pruning of evicted folders on disk."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            website_dir = tmp_path / "website"
            build_dir = tmp_path / "build"
            website_dir.mkdir()
            build_dir.mkdir()

            # Create dummy binaries
            for bin_name in ["bootloader.bin", "partitions.bin", "boot_app0.bin", "firmware.bin"]:
                (build_dir / bin_name).write_bytes(b"\xAA" * 128)

            device_id = "waveshare-s3-43b"

            # 1. Sync v0.1.0 (Stable)
            res1 = sync_website_flasher.sync_device_release(
                website_dir=website_dir,
                device_id=device_id,
                device_name="Waveshare 4.3\" Touch",
                chip_family="ESP32-S3",
                tag="v0.1.0",
                is_prerelease=False,
                build_dir=build_dir,
            )
            self.assertIn("v0.1.0", res1["retained_tags"])
            self.assertTrue((website_dir / "bin" / device_id / "v0.1.0" / "firmware.bin").is_file())
            v010_manifest_path = website_dir / "manifests" / device_id / "manifest-v0.1.0.json"
            self.assertTrue(v010_manifest_path.is_file())
            v010_manifest = json.loads(v010_manifest_path.read_text(encoding="utf-8"))
            v010_parts = v010_manifest["builds"][0]["parts"]
            self.assertEqual(v010_parts[0]["path"], f"../../bin/{device_id}/v0.1.0/bootloader.bin")
            self.assertTrue(all(p["path"].startswith(f"../../bin/{device_id}/v0.1.0/") for p in v010_parts))

            # 2. Sync v0.1.1 (Stable)
            sync_website_flasher.sync_device_release(
                website_dir=website_dir,
                device_id=device_id,
                device_name="Waveshare 4.3\" Touch",
                chip_family="ESP32-S3",
                tag="v0.1.1",
                is_prerelease=False,
                build_dir=build_dir,
            )

            # 3. Sync v0.1.2 (Stable) -> Should evict v0.1.0!
            res3 = sync_website_flasher.sync_device_release(
                website_dir=website_dir,
                device_id=device_id,
                device_name="Waveshare 4.3\" Touch",
                chip_family="ESP32-S3",
                tag="v0.1.2",
                is_prerelease=False,
                build_dir=build_dir,
            )
            self.assertIn("v0.1.0", res3["pruned_tags"])
            # Ensure v0.1.0 folder was deleted
            self.assertFalse((website_dir / "bin" / device_id / "v0.1.0").exists())
            self.assertFalse((website_dir / "manifests" / device_id / "manifest-v0.1.0.json").exists())
            # Ensure v0.1.1 and v0.1.2 remain
            self.assertTrue((website_dir / "bin" / device_id / "v0.1.1").exists())
            self.assertTrue((website_dir / "bin" / device_id / "v0.1.2").exists())

            # 4. Verify root legacy manifests still use bin/<device_id>/... relative to site root
            for root_name in ("manifest-stable.json", "manifest.json"):
                root_manifest = json.loads((website_dir / root_name).read_text(encoding="utf-8"))
                self.assertEqual(
                    root_manifest["builds"][0]["parts"][0]["path"],
                    f"bin/{device_id}/v0.1.2/bootloader.bin",
                )

            # 4. Verify releases.json
            releases_json = json.loads((website_dir / "releases.json").read_text(encoding="utf-8"))
            self.assertEqual(releases_json["schema_version"], 2)
            dev_data = releases_json["devices"][device_id]
            self.assertEqual(dev_data["latest_stable"], "v0.1.2")
            versions_in_json = [r["version"] for r in dev_data["releases"]]
            self.assertEqual(versions_in_json, ["v0.1.2", "v0.1.1"])

    def test_07_multi_device_isolation(self):
        """Verifies that release pruning for device B does NOT affect device A."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            website_dir = tmp_path / "website"
            build_dir = tmp_path / "build"
            website_dir.mkdir()
            build_dir.mkdir()

            for bin_name in ["bootloader.bin", "partitions.bin", "boot_app0.bin", "firmware.bin"]:
                (build_dir / bin_name).write_bytes(b"\xBB" * 128)

            # Add Device A releases
            for tag in ["v0.1.0", "v0.1.1"]:
                sync_website_flasher.sync_device_release(
                    website_dir=website_dir,
                    device_id="touch-43b",
                    device_name="Touch 4.3B",
                    chip_family="ESP32-S3",
                    tag=tag,
                    is_prerelease=False,
                    build_dir=build_dir,
                )

            # Add Device B releases (3 patches -> evicts B's v0.1.0)
            for tag in ["v0.1.0", "v0.1.1", "v0.1.2"]:
                sync_website_flasher.sync_device_release(
                    website_dir=website_dir,
                    device_id="touch-70",
                    device_name="Touch 7.0",
                    chip_family="ESP32-S3",
                    tag=tag,
                    is_prerelease=False,
                    build_dir=build_dir,
                )

            # Device A's v0.1.0 must still exist!
            self.assertTrue((website_dir / "bin" / "touch-43b" / "v0.1.0").exists())
            # Device B's v0.1.0 was evicted!
            self.assertFalse((website_dir / "bin" / "touch-70" / "v0.1.0").exists())


if __name__ == "__main__":
    unittest.main()
