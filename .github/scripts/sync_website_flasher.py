#!/usr/bin/env python3
"""
sync_website_flasher.py
Automated Multi-Device Web Flasher Synchronizer & Semantic Retention Pruner.

Responsibilities:
  1. Namespaces firmware binaries and manifests by target hardware ID (device_id).
  2. Copies compiled ESP32 binaries into bin/<device_id>/<tag>/
  3. Generates ESP Web Tools manifests at manifests/<device_id>/manifest-<tag>.json
  4. Enforces the strict semantic retention policy per device:
     - Pre-releases: Keep the last 5 builds regardless of major.minor.patch.
     - Stable releases:
       * Major switch: keep only the last stable release of (current_major - 1). Older majors pruned.
       * Minor switch: keep the last 2 minor versions (current_minor and current_minor - 1). Older minors pruned.
       * Current minor: keep the last 2 patch releases.
       * Prior minor: keep its last stable release.
  5. Prunes evicted binary folders and manifest files to prevent git storage bloat.
  6. Updates master releases.json (Schema v2) and root legacy manifest aliases.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

SEMVER_REGEX = re.compile(
    r"^v?(\d+)\.(\d+)\.(\d+)(?:-(?:rc|dev)\.?(\d{8})(?:\.(\d+))?)?$",
    re.IGNORECASE,
)


def parse_semver(tag: str) -> Optional[Tuple[int, int, int, int, int, int]]:
    """
    Parses semantic versioning tag into a comparable tuple:
    (major, minor, patch, is_stable, rc_date, rc_iter)

    Stable release: is_stable = 1
    Pre-release (RC/dev): is_stable = 0
    """
    if not tag:
        return None
    m = SEMVER_REGEX.match(tag.strip())
    if not m:
        return None
    major = int(m.group(1))
    minor = int(m.group(2))
    patch = int(m.group(3))
    rc_date = m.group(4)
    rc_iter = m.group(5)

    if rc_date:
        return (major, minor, patch, 0, int(rc_date), int(rc_iter) if rc_iter else 1)
    else:
        return (major, minor, patch, 1, 0, 0)


def determine_retained_versions(tags: List[str]) -> Set[str]:
    """
    Applies the StoryDeck retention policy to a list of tags:

    1. Pre-releases:
       - Keep the last 5 builds regardless of (major, minor, patch).
    2. Stable releases:
       - Determine current_major (highest major among stable releases).
       - Major switch rule:
         * If major versions < current_major exist, keep ONLY the last stable release of (current_major - 1).
         * Prune all older major versions (< current_major - 1).
       - For current_major:
         * Minor switch rule: Keep the last 2 minor versions (current_minor and current_minor - 1).
         * Older minor versions in current_major (< current_minor - 1) are pruned.
         * For current_minor: Keep the last 2 patch versions.
         * For current_minor - 1: Keep its last stable release (highest patch).
    """
    stables: List[Tuple[Tuple[int, int, int, int, int, int], str]] = []
    prereleases: List[Tuple[Tuple[int, int, int, int, int, int], str]] = []

    for t in set(tags):
        p = parse_semver(t)
        if not p:
            continue
        if p[3] == 1:
            stables.append((p, t))
        else:
            prereleases.append((p, t))

    retained: Set[str] = set()

    # Rule 1: Pre-releases — Keep last 5 builds
    prereleases.sort(key=lambda x: x[0], reverse=True)
    for _, tag_str in prereleases[:5]:
        retained.add(tag_str)

    # Rule 2: Stable releases
    if not stables:
        return retained

    stables.sort(key=lambda x: x[0], reverse=True)
    all_majors = sorted(list({s[0][0] for s in stables}), reverse=True)
    current_major = all_majors[0]

    # Major switch: Keep only the latest release of (current_major - 1)
    if len(all_majors) > 1:
        prev_major = all_majors[1]
        prev_major_releases = [s for s in stables if s[0][0] == prev_major]
        if prev_major_releases:
            retained.add(prev_major_releases[0][1])

    # Within current_major:
    current_major_stables = [s for s in stables if s[0][0] == current_major]
    minors = sorted(list({s[0][1] for s in current_major_stables}), reverse=True)

    if minors:
        current_minor = minors[0]
        # Keep last 2 patch releases in current_minor
        current_minor_stables = [s for s in current_major_stables if s[0][1] == current_minor]
        for _, tag_str in current_minor_stables[:2]:
            retained.add(tag_str)

        # Minor switch: Keep last stable of (current_minor - 1)
        if len(minors) > 1:
            prev_minor = minors[1]
            prev_minor_stables = [s for s in current_major_stables if s[0][1] == prev_minor]
            if prev_minor_stables:
                retained.add(prev_minor_stables[0][1])

    return retained


def generate_manifest_data(
    device_name: str,
    chip_family: str,
    tag: str,
    device_id: str,
) -> Dict[str, Any]:
    """Builds standard ESP Web Tools manifest JSON structure."""
    return {
        "name": f"{device_name} ({tag})",
        "version": tag,
        "home_assistant_domain": "storydeck",
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": chip_family,
                "parts": [
                    {"path": f"bin/{device_id}/{tag}/bootloader.bin", "offset": 0},
                    {"path": f"bin/{device_id}/{tag}/partitions.bin", "offset": 32768},
                    {"path": f"bin/{device_id}/{tag}/boot_app0.bin", "offset": 57344},
                    {"path": f"bin/{device_id}/{tag}/firmware.bin", "offset": 65536},
                ],
            }
        ],
    }


def sync_device_release(
    website_dir: Path,
    device_id: str,
    device_name: str,
    chip_family: str,
    tag: str,
    is_prerelease: bool,
    build_dir: Path,
) -> Dict[str, Any]:
    """
    Executes the sync, manifest creation, and retention pruning workflow.
    """
    website_dir = website_dir.resolve()
    if not website_dir.exists():
        raise FileNotFoundError(f"Website repository directory not found: {website_dir}")

    # Destination directories
    bin_tag_dir = website_dir / "bin" / device_id / tag
    manifest_dir = website_dir / "manifests" / device_id
    bin_tag_dir.mkdir(parents=True, exist_ok=True)
    manifest_dir.mkdir(parents=True, exist_ok=True)

    # 1. Copy required binary parts
    parts_map = {
        "bootloader.bin": build_dir / "bootloader.bin",
        "partitions.bin": build_dir / "partitions.bin",
        "firmware.bin": build_dir / "firmware.bin",
    }
    for dest_name, src_path in parts_map.items():
        if not src_path.is_file():
            raise FileNotFoundError(f"Missing build artifact: {src_path}")
        shutil.copy2(src_path, bin_tag_dir / dest_name)

    # boot_app0.bin fallback discovery
    boot_app0_src = build_dir / "boot_app0.bin"
    if not boot_app0_src.is_file():
        # Fallback to existing website boot_app0.bin
        root_boot_app0 = website_dir / "bin" / "boot_app0.bin"
        if root_boot_app0.is_file():
            boot_app0_src = root_boot_app0
    if boot_app0_src.is_file():
        shutil.copy2(boot_app0_src, bin_tag_dir / "boot_app0.bin")
    else:
        raise FileNotFoundError(f"boot_app0.bin could not be found in {build_dir} or {website_dir}/bin/")

    # 2. Write versioned manifest
    manifest_file = manifest_dir / f"manifest-{tag}.json"
    manifest_data = generate_manifest_data(device_name, chip_family, tag, device_id)
    manifest_file.write_text(json.dumps(manifest_data, indent=2), encoding="utf-8")

    # 3. Load or initialize releases.json (Schema v2)
    releases_json_path = website_dir / "releases.json"
    if releases_json_path.exists():
        try:
            releases_data = json.loads(releases_json_path.read_text(encoding="utf-8"))
        except Exception:
            releases_data = {}
    else:
        releases_data = {}

    if not isinstance(releases_data, dict):
        releases_data = {}

    releases_data["schema_version"] = 2
    if "default_device" not in releases_data:
        releases_data["default_device"] = device_id

    devices_dict = releases_data.setdefault("devices", {})
    device_entry = devices_dict.setdefault(device_id, {
        "name": device_name,
        "chip_family": chip_family,
        "latest_stable": None,
        "latest_prerelease": None,
        "releases": [],
    })

    # Update metadata
    device_entry["name"] = device_name
    device_entry["chip_family"] = chip_family

    # 4. Upsert this release record
    channel = "prerelease" if is_prerelease else "stable"
    now_iso = datetime.now(timezone.utc).isoformat()
    existing_releases: List[Dict[str, Any]] = device_entry.get("releases", [])

    # Check if entry already exists
    fw_rel_path = f"bin/{device_id}/{tag}/firmware.bin"
    fw_sha256 = hashlib.sha256((bin_tag_dir / "firmware.bin").read_bytes()).hexdigest()
    matched = False
    for r in existing_releases:
        if r.get("version") == tag:
            r["channel"] = channel
            r["manifest"] = f"manifests/{device_id}/manifest-{tag}.json"
            r["firmware"] = fw_rel_path
            r["firmware_sha256"] = fw_sha256
            r["published_at"] = r.get("published_at") or now_iso
            matched = True
            break
    if not matched:
        existing_releases.append({
            "version": tag,
            "channel": channel,
            "name": f"{device_name} {tag} ({channel.capitalize()})",
            "manifest": f"manifests/{device_id}/manifest-{tag}.json",
            "firmware": fw_rel_path,
            "firmware_sha256": fw_sha256,
            "published_at": now_iso,
        })

    # 5. Apply retention policy calculation
    all_known_tags = [r["version"] for r in existing_releases]
    retained_tags = determine_retained_versions(all_known_tags)

    # 6. Prune evicted versions from disk and manifest list
    pruned_tags = set(all_known_tags) - retained_tags
    pruned_count = 0
    for pt in pruned_tags:
        # Delete binary dir
        pt_bin = website_dir / "bin" / device_id / pt
        if pt_bin.exists():
            shutil.rmtree(pt_bin, ignore_errors=True)

        # Delete manifest file
        pt_manifest = website_dir / "manifests" / device_id / f"manifest-{pt}.json"
        if pt_manifest.exists():
            pt_manifest.unlink(missing_ok=True)

        pruned_count += 1
        print(f"🗑️ Pruned evicted build for {device_id}: {pt}")

    # Keep only retained release entries in releases.json and ensure firmware/sha256 fields exist
    kept_releases = [r for r in existing_releases if r.get("version") in retained_tags]
    for r in kept_releases:
        ver = r.get("version", "")
        fw_file = website_dir / "bin" / device_id / ver / "firmware.bin"
        if fw_file.is_file():
            r.setdefault("firmware", f"bin/{device_id}/{ver}/firmware.bin")
            if not r.get("firmware_sha256"):
                r["firmware_sha256"] = hashlib.sha256(fw_file.read_bytes()).hexdigest()
    # Sort releases descending by semver tuple
    kept_releases.sort(key=lambda r: parse_semver(r.get("version", "")) or (0, 0, 0, 0, 0, 0), reverse=True)
    device_entry["releases"] = kept_releases

    # 7. Update latest_stable and latest_prerelease markers
    latest_stable_tag = next((r["version"] for r in kept_releases if r["channel"] == "stable"), None)
    latest_prerelease_tag = next((r["version"] for r in kept_releases if r["channel"] == "prerelease"), None)
    device_entry["latest_stable"] = latest_stable_tag
    device_entry["latest_prerelease"] = latest_prerelease_tag

    # 8. Save updated releases.json
    releases_json_path.write_text(json.dumps(releases_data, indent=2), encoding="utf-8")

    # 9. Update root legacy aliases for backward compatibility if default device
    if releases_data.get("default_device") == device_id:
        if latest_stable_tag:
            stable_manifest_path = website_dir / "manifest-stable.json"
            stable_data = generate_manifest_data(device_name, chip_family, latest_stable_tag, device_id)
            stable_manifest_path.write_text(json.dumps(stable_data, indent=2), encoding="utf-8")
            # Root manifest.json mirrors stable
            (website_dir / "manifest.json").write_text(json.dumps(stable_data, indent=2), encoding="utf-8")

        if latest_prerelease_tag:
            prerelease_manifest_path = website_dir / "manifest-prerelease.json"
            prerelease_data = generate_manifest_data(device_name, chip_family, latest_prerelease_tag, device_id)
            prerelease_manifest_path.write_text(json.dumps(prerelease_data, indent=2), encoding="utf-8")

    print(f"✅ Successfully synced {device_id} release {tag} ({channel}).")
    print(f"   Retained: {len(retained_tags)} builds (Pruned: {pruned_count})")
    print(f"   Latest Stable: {latest_stable_tag or 'None'}")
    print(f"   Latest Pre-Release: {latest_prerelease_tag or 'None'}")

    return {
        "device_id": device_id,
        "tag": tag,
        "channel": channel,
        "retained_tags": sorted(list(retained_tags)),
        "pruned_tags": sorted(list(pruned_tags)),
        "latest_stable": latest_stable_tag,
        "latest_prerelease": latest_prerelease_tag,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Synchronize firmware builds to website flasher with retention pruning.")
    parser.add_argument("--website-dir", required=True, help="Path to cloned website branch.")
    parser.add_argument("--device-id", default="waveshare-s3-43b", help="Target hardware environment ID.")
    parser.add_argument("--device-name", default="Waveshare 4.3\" ESP32-S3 Touch (LCD-4.3B)", help="Human-readable device name.")
    parser.add_argument("--chip-family", default="ESP32-S3", help="Target chip family (e.g. ESP32-S3).")
    parser.add_argument("--build-dir", required=True, help="Path to PlatformIO build output directory.")
    parser.add_argument("--tag", required=True, help="Release version tag (e.g. v0.1.0).")
    parser.add_argument("--prerelease", default="false", help="'true' if pre-release / RC build, else 'false'.")

    args = parser.parse_args()
    is_pre = str(args.prerelease).strip().lower() in ("true", "1", "yes")

    sync_device_release(
        website_dir=Path(args.website_dir),
        device_id=args.device_id.strip(),
        device_name=args.device_name.strip(),
        chip_family=args.chip_family.strip(),
        tag=args.tag.strip(),
        is_prerelease=is_pre,
        build_dir=Path(args.build_dir),
    )


if __name__ == "__main__":
    main()
