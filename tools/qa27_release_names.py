#!/usr/bin/env python3
"""User-visible 2.7+ release names. Fail closed; preserve all prior exports."""
import argparse
import hashlib
import json
import os
import tempfile
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "docs/ep128emu_release_contract.json"
PLATFORMS = ("mac_universal","mac_arm64","win64","win32","linux_x86_64")


def expected(version, platform):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,2}", version):
        raise ValueError("INVALID_RELEASE_VERSION")
    if version != "2.1.1" and tuple(map(int,version.split("."))) < (2,7):
        raise ValueError("SHORT_NAME_POLICY_APPLIES_TO_2_1_1_PUBLIC_OR_2_7_TRIAL")
    data=json.loads(POLICY.read_text())
    templates=data["release_2_7_extension"]["file_names"]
    if tuple(templates)!=PLATFORMS or platform not in templates:
        raise ValueError("UNAPPROVED_PLATFORM")
    result=templates[platform].format(version=version)
    if not re.fullmatch(
        rf"ep128emu-{re.escape(version)}-(?:mac|mac-arm64|win64|win32|linux)"
        r"\.(?:zip|exe|tar\.gz)",result):
        raise ValueError("VERBOSE_OR_INVALID_HANDOFF_NAME")
    return result


def filehash(path):
    sha=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""):
            sha.update(chunk)
    return sha.hexdigest()


def export(source, destdir, version, platform):
    name=expected(version,platform)
    source=Path(source)
    destdir=Path(destdir)
    if not source.is_file() or not destdir.is_dir():
        raise ValueError("SOURCE_OR_DELIVERY_DIRECTORY_MISSING")
    destination=destdir/name
    if destination.exists() or destination.is_symlink():
        raise ValueError("REFUSE_OVERWRITE_EXISTING_RELEASE_NAME:"+name)
    with source.open("rb") as f:
        magic=f.read(4)
    if platform.startswith("win") and magic[:2]!=b"MZ":
        raise ValueError("NOT_A_WINDOWS_INSTALLER_PE")
    if platform.startswith("mac") and magic!=b"PK\x03\x04":
        raise ValueError("NOT_A_MAC_ZIP")
    if platform=="linux_x86_64" and magic[:2]!=b"\x1f\x8b":
        raise ValueError("NOT_A_LINUX_GZIP")
    original_sha=filehash(source)
    # Stage a verified complete file, then create the user-visible name
    # with an exclusive hardlink. Never rename/overwrite an old release;
    # even a race with another exporter must preserve the previous bytes.
    temp_path=None
    try:
        with tempfile.NamedTemporaryFile(
                prefix=".ep128emu-artifact-", suffix=".tmp",
                dir=destdir, delete=False) as out:
            temp_path=Path(out.name)
            with source.open("rb") as src:
                for chunk in iter(lambda:src.read(1024*1024),b""):
                    out.write(chunk)
            out.flush()
            os.fsync(out.fileno())
        if filehash(temp_path)!=original_sha:
            raise ValueError("EXPORTED_ARTIFACT_SHA_MISMATCH")
        try:
            os.link(temp_path,destination)
        except FileExistsError as exc:
            raise ValueError("REFUSE_OVERWRITE_EXISTING_RELEASE_NAME:"+name) from exc
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()
    print("STRICT_SHORT_RELEASE_NAME_PASS", destination.name,
          destination.stat().st_size, original_sha)
    return destination


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--platform",choices=PLATFORMS,required=True)
    p.add_argument("--version",required=True)
    p.add_argument("--source")
    p.add_argument("--dest",default=str(Path.home()/"Downloads/ep128emu"))
    p.add_argument("--expected-name",action="store_true")
    a=p.parse_args()
    if a.expected_name:
        print(expected(a.version,a.platform))
    else:
        if not a.source:
            raise ValueError("SOURCE_REQUIRED")
        export(a.source,a.dest,a.version,a.platform)


if __name__=="__main__":
    try:main()
    except (ValueError,OSError) as e:
        print("RELEASE_SHORT_NAME_GATE_FAIL",e,file=sys.stderr)
        sys.exit(1)
