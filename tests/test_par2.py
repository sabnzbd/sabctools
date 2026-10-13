#!/usr/bin/python3 -OO
# Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

import gc
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import textwrap
import threading
import zlib

import pytest

import sabctools

PAR2FILES = os.path.join(os.path.dirname(__file__), "par2files")
DATA_FILES = ("alpha.bin", "beta.bin", "gamma.bin")

# The fixture set has a 64 byte block size, 1920 source blocks and 576 recovery blocks
# (30% redundancy). Per file: alpha 960, beta 640, gamma 320. So losing gamma, or gamma
# plus some of alpha, is repairable; losing beta or alpha outright is not. Keep that
# budget in mind when changing which files a test damages.
RECOVERY_BLOCKS = 576


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


@pytest.fixture
def par2set(tmp_path):
    """A pristine copy of the fixture set, so tests can damage it freely."""
    for name in os.listdir(PAR2FILES):
        shutil.copy(os.path.join(PAR2FILES, name), tmp_path)
    return str(tmp_path)


@pytest.fixture
def digests():
    return {name: md5(os.path.join(PAR2FILES, name)) for name in DATA_FILES}


def rewrite_creator(path, transform):
    """Rewrite the client text of each creator packet in a par2 file, keeping its length
    and recomputing the packet hash so par2 still accepts it."""
    with open(path, "rb") as f:
        data = bytearray(f.read())
    offset = data.find(b"PAR 2.0\x00Creator\x00")
    while offset >= 0:
        start = offset - 48
        length = struct.unpack_from("<Q", data, start + 8)[0]
        body = transform(bytes(data[start + 64 : start + length]))
        data[start + 64 : start + length] = body
        data[start + 16 : start + 32] = hashlib.md5(data[start + 32 : start + length]).digest()
        offset = data.find(b"PAR 2.0\x00Creator\x00", start + length)
    with open(path, "wb") as f:
        f.write(data)


def repairer(basepath, **kwargs):
    return sabctools.Par2Repairer(os.path.join(basepath, "rec.par2"), basepath=basepath, **kwargs)


class TestPar2Load:
    def test_the_result_is_a_par2result(self, par2set):
        # Not the bare int behind it, so callers can report which one it was
        assert repairer(par2set).load().name == "SUCCESS"

    def test_reads_the_set(self, par2set):
        rep = repairer(par2set)
        assert rep.load() == sabctools.Par2Result.SUCCESS
        assert rep.recoverable_file_count == 3
        assert rep.source_block_count > 0
        assert rep.recovery_block_count > 0
        assert rep.block_size > 0
        # The recovery set id sits at offset 32 of every packet header
        with open(os.path.join(par2set, "rec.par2"), "rb") as f:
            assert rep.setid == f.read(48)[32:48]

    def test_reports_the_client_that_created_the_set(self, par2set):
        rep = repairer(par2set)
        assert rep.creator == ""
        rep.load()
        # The zero padding that rounds the packet to four bytes is not part of it
        assert rep.creator == "Created by par2cmdline-turbo version 1.1.1."

    def test_a_creator_which_is_not_utf8_does_not_raise(self, par2set):
        for name in ("rec.par2", "rec.vol000+576.par2"):
            rewrite_creator(os.path.join(par2set, name), lambda body: body.replace(b"Created", b"Cr\xe9ated"))
        rep = repairer(par2set)
        assert rep.load() == sabctools.Par2Result.SUCCESS
        assert rep.creator.startswith("Cr\ufffdated by par2cmdline-turbo")

    def test_verify_before_load_is_rejected(self, par2set):
        rep = repairer(par2set)
        with pytest.raises(RuntimeError):
            rep.verify()

    def test_missing_parfile_is_reported_by_load(self, tmp_path):
        # Construction reads nothing, so a path that is not there surfaces as a load()
        # result rather than at construction
        rep = sabctools.Par2Repairer(str(tmp_path / "nope.par2"), basepath=str(tmp_path))
        assert rep.load() == sabctools.Par2Result.FILE_IO_ERROR

    @pytest.mark.parametrize(
        "kwargs",
        [
            {"memory_limit": 64 * 1024 * 1024},
            {"memory_limit": 1024},  # below a megabyte, must not become -m0
            {"threads": 2},
            {"file_threads": 1},
            {"skip_data": False},
            {"skip_leaway": 128},
            {"skip_repaired_verification": False},
        ],
    )
    def test_options_are_accepted(self, par2set, kwargs):
        rep = repairer(par2set, **kwargs)
        assert rep.load() == sabctools.Par2Result.SUCCESS

    def test_load_reports_block_budget(self, par2set):
        rep = repairer(par2set)
        rep.load()
        assert rep.recovery_block_count == RECOVERY_BLOCKS
        assert rep.source_block_count == 1920


class TestPar2SetChecksums:
    """What the par2 packets say the data should be, as opposed to what is on disk."""

    def test_hash16k_is_the_md5_of_the_first_16k(self, par2set):
        rep = repairer(par2set)
        rep.load()
        for entry in rep.files:
            with open(os.path.join(par2set, entry["name"]), "rb") as f:
                expected = hashlib.md5(f.read(16384)).digest()
            assert entry["hash16k"] == expected

    def test_block_checksums_match_the_data(self, par2set):
        rep = repairer(par2set)
        rep.load()
        blocksize = rep.block_size
        for entry in rep.files:
            crcs = rep.block_checksums(entry["name"])
            assert len(crcs) == entry["blocks"]
            with open(os.path.join(par2set, entry["name"]), "rb") as f:
                data = f.read()
            for index, crc in enumerate(crcs):
                # par2 hashes the final block padded out to the block size
                block = data[index * blocksize : (index + 1) * blocksize].ljust(blocksize, b"\0")
                assert zlib.crc32(block) == crc

    def test_block_checksums_of_a_file_outside_the_set_is_none(self, par2set):
        rep = repairer(par2set)
        rep.load()
        assert rep.block_checksums("not-in-the-set.bin") is None

    def test_a_name_which_is_not_utf8_is_passed_through(self, par2set):
        rep = repairer(par2set)
        rep.load()
        assert rep.block_checksums("caf\udce9.bin") is None

    def test_block_checksums_before_load_is_rejected(self, par2set):
        rep = repairer(par2set)
        with pytest.raises(RuntimeError):
            rep.block_checksums("alpha.bin")


class TestPar2Verify:
    def test_all_files_correct(self, par2set):
        rep = repairer(par2set)
        rep.load()
        assert rep.verify() == sabctools.Par2Result.SUCCESS
        assert rep.complete_file_count == 3
        assert rep.damaged_file_count == 0
        assert rep.missing_file_count == 0
        assert rep.missing_block_count == 0
        assert rep.repair_possible
        assert rep.renames == {}

    def test_damaged_file_is_detected(self, par2set):
        with open(os.path.join(par2set, "alpha.bin"), "r+b") as f:
            f.seek(20000)
            f.write(b"CORRUPTED" * 100)

        rep = repairer(par2set)
        rep.load()
        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert rep.damaged_file_count == 1
        assert rep.complete_file_count == 2
        assert rep.missing_block_count > 0
        assert rep.repair_possible

    def test_missing_file_is_detected(self, par2set):
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        rep.load()
        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert rep.missing_file_count == 1
        assert rep.repair_possible

        gamma = next(f for f in rep.files if f["name"] == "gamma.bin")
        assert gamma["target"] == os.path.join(par2set, "gamma.bin")
        assert gamma["blocks"] == 320

    def test_file_details(self, par2set):
        rep = repairer(par2set)
        rep.load()
        rep.verify()
        assert sorted(f["name"] for f in rep.files) == sorted(DATA_FILES)
        for entry in rep.files:
            assert entry["blocks"] > 0
            assert entry["size"] > 0
            assert entry["target"] == os.path.join(par2set, entry["name"])


class TestPar2Repair:
    def test_repairs_damaged_and_missing(self, par2set, digests):
        with open(os.path.join(par2set, "alpha.bin"), "r+b") as f:
            f.seek(20000)
            f.write(b"CORRUPTED" * 100)
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        rep.load()
        rep.verify()
        assert rep.repair() == sabctools.Par2Result.SUCCESS

        for name, expected in digests.items():
            assert md5(os.path.join(par2set, name)) == expected

    @staticmethod
    def damage_and_repair(par2set, **kwargs):
        with open(os.path.join(par2set, "alpha.bin"), "r+b") as f:
            f.seek(20000)
            f.write(b"CORRUPTED" * 100)

        warnings = []
        rep = repairer(par2set, **kwargs)
        rep.warning_callback = lambda code, message, filename: warnings.append(code)
        rep.load()
        rep.verify()
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        return warnings, rep.in_place_files

    def test_a_damaged_file_is_repaired_in_place(self, par2set, digests):
        warnings, in_place_files = self.damage_and_repair(par2set)

        assert warnings == []
        assert in_place_files == ["alpha.bin"]
        assert md5(os.path.join(par2set, "alpha.bin")) == digests["alpha.bin"]
        assert not os.path.exists(os.path.join(par2set, "alpha.bin.1"))

    def test_in_place_repair_can_be_turned_off(self, par2set, digests):
        _, in_place_files = self.damage_and_repair(par2set, in_place_repair=False)

        assert in_place_files == []

        assert md5(os.path.join(par2set, "alpha.bin")) == digests["alpha.bin"]
        assert os.path.exists(os.path.join(par2set, "alpha.bin.1"))

    def test_data_skipping_writes_a_new_copy(self, par2set, digests):
        warnings, _ = self.damage_and_repair(par2set, skip_data=True)

        assert sabctools.Par2WarningCode.NOT_REPAIRED_IN_PLACE in warnings
        assert md5(os.path.join(par2set, "alpha.bin")) == digests["alpha.bin"]
        assert os.path.exists(os.path.join(par2set, "alpha.bin.1"))

    def test_repair_without_verify(self, par2set, digests):
        """repair() runs the verification pass itself when verify() was not called."""
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        rep.load()
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        assert md5(os.path.join(par2set, "gamma.bin")) == digests["gamma.bin"]

    def test_single_large_file_loss_exceeds_recovery(self, par2set):
        # beta.bin is 640 blocks against 576 recovery blocks
        os.remove(os.path.join(par2set, "beta.bin"))

        rep = repairer(par2set)
        rep.load()
        assert rep.verify() == sabctools.Par2Result.REPAIR_NOT_POSSIBLE
        assert not rep.repair_possible
        assert rep.missing_block_count == 640

    def test_insufficient_blocks(self, par2set):
        # Two of three files gone is more than 30% redundancy can rebuild
        os.remove(os.path.join(par2set, "alpha.bin"))
        os.remove(os.path.join(par2set, "beta.bin"))

        rep = repairer(par2set)
        rep.load()
        assert rep.verify() == sabctools.Par2Result.REPAIR_NOT_POSSIBLE
        assert not rep.repair_possible
        assert rep.missing_block_count > rep.recovery_block_count
        # The shortfall drives how many extra par2 blocks a caller needs to fetch
        assert rep.missing_block_count - rep.recovery_block_count > 0
        assert rep.repair() == sabctools.Par2Result.REPAIR_NOT_POSSIBLE


class TestPar2KnownBlocks:
    """Quick verify: blocks the download already vouched for are never read."""

    def damage(self, par2set, name="alpha.bin"):
        with open(os.path.join(par2set, name), "r+b") as f:
            f.seek(1000)
            f.write(b"\x5a" * 2000)

    def test_vouched_blocks_are_not_read(self, par2set):
        # Damage that a scan finds, so taking the caller's word is visible in the result
        self.damage(par2set)

        rep = repairer(par2set)
        rep.load()
        rep.set_known_blocks({entry["name"]: [True] * entry["blocks"] for entry in rep.files})

        assert rep.verify() == sabctools.Par2Result.SUCCESS
        assert rep.quick_verified_files == 3
        assert rep.damaged_file_count == 0

    def test_the_same_damage_is_found_when_scanned(self, par2set):
        self.damage(par2set)

        rep = repairer(par2set)
        rep.load()

        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert rep.quick_verified_files == 0

    def test_a_file_written_off_is_not_read_either(self, par2set):
        rep = repairer(par2set)
        rep.load()
        alpha = next(entry for entry in rep.files if entry["name"] == "alpha.bin")
        rep.set_known_blocks({"alpha.bin": [False] * alpha["blocks"]})

        # alpha is 960 of the 1920 source blocks, more than the 576 recovery blocks
        assert rep.verify() == sabctools.Par2Result.REPAIR_NOT_POSSIBLE
        assert rep.missing_block_count == alpha["blocks"]

    def test_vouching_is_per_block(self, par2set):
        """Blocks vouched for are taken without being read, and unknown ones are looked for."""
        rep = repairer(par2set)
        rep.load()
        alpha = next(entry for entry in rep.files if entry["name"] == "alpha.bin")

        # alpha is 960 blocks of 64 bytes, so the second half starts at 30720
        half = alpha["blocks"] // 2
        with open(os.path.join(par2set, "alpha.bin"), "r+b") as f:
            f.seek(half * 64 + 100)
            f.write(b"\x5a" * 2000)

        # the damage is in the half vouched for, so it goes unseen, and the intact half
        # is found where it belongs
        rep.set_known_blocks({"alpha.bin": [None] * half + [True] * (alpha["blocks"] - half)})
        assert rep.verify() == sabctools.Par2Result.SUCCESS

        # the same damage, scanned rather than vouched for, is a real shortfall
        scanned = repairer(par2set)
        scanned.load()
        assert scanned.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert scanned.missing_block_count > 0

    def test_blocks_said_to_be_missing_are_not_looked_for(self, par2set):
        rep = repairer(par2set)
        rep.load()
        alpha = next(entry for entry in rep.files if entry["name"] == "alpha.bin")

        half = alpha["blocks"] // 2
        rep.set_known_blocks({"alpha.bin": [False] * half + [True] * (alpha["blocks"] - half)})
        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert rep.missing_block_count == half

    def test_a_repair_forgets_what_was_vouched_for(self, par2set):
        self.damage(par2set)

        rep = repairer(par2set)
        rep.load()
        alpha = next(entry for entry in rep.files if entry["name"] == "alpha.bin")

        # The damage is in the first half, so the second is vouched for truthfully
        half = alpha["blocks"] // 2
        rep.set_known_blocks({"alpha.bin": [False] * half + [True] * (alpha["blocks"] - half)})
        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        assert rep.quick_verified_files == 0
        assert rep.verify() == sabctools.Par2Result.SUCCESS

    @staticmethod
    def repair_stages(par2set, known) -> set:
        os.remove(os.path.join(par2set, "gamma.bin"))
        rep = repairer(par2set)
        rep.load()
        gamma = next(entry for entry in rep.files if entry["name"] == "gamma.bin")
        rep.set_known_blocks({name: blocks(gamma["blocks"]) for name, blocks in known.items()})
        stages = set()
        rep.progress_callback = lambda stage, filename, percent: stages.add(stage)
        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        return stages

    def test_the_repair_is_read_back_when_nothing_was_said_present(self, par2set):
        stages = self.repair_stages(par2set, {"gamma.bin": lambda count: [False] * count})
        assert "verifying_repair" in stages

    def test_the_repair_is_not_read_back_when_a_block_was_said_present(self, par2set):
        stages = self.repair_stages(par2set, {"gamma.bin": lambda count: [False] * (count - 1) + [True]})
        assert "verifying_repair" not in stages

    def test_a_second_call_retracts_what_the_first_said(self, par2set):
        self.damage(par2set)

        rep = repairer(par2set)
        rep.load()
        rep.set_known_blocks({entry["name"]: [True] * entry["blocks"] for entry in rep.files})
        assert rep.quick_verified_files == 3

        rep.set_known_blocks({})
        assert rep.quick_verified_files == 0
        assert rep.verify() == sabctools.Par2Result.REPAIR_POSSIBLE

    def test_before_load_is_rejected(self, par2set):
        rep = repairer(par2set)
        with pytest.raises(RuntimeError):
            rep.set_known_blocks({"alpha.bin": [True]})

    def test_a_non_mapping_is_rejected(self, par2set):
        rep = repairer(par2set)
        rep.load()
        with pytest.raises(TypeError):
            rep.set_known_blocks([("alpha.bin", [True])])


class TestPar2VerifyFile:
    """Feeding files in one at a time, which is how a download arrives."""

    def test_scans_only_the_named_file(self, par2set):
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        rep.load()

        seen = []
        rep.file_done_callback = lambda name, found, total: seen.append(name)

        rep.verify_file(os.path.join(par2set, "alpha.bin"))
        assert seen == ["alpha.bin"]
        assert rep.complete_file_count == 1

    def test_files_fed_from_several_threads_take_turns(self, par2set):
        names = ["alpha.bin", "beta.bin", "gamma.bin"]

        alone = repairer(par2set)
        alone.load()
        for name in names:
            alone.verify_file(os.path.join(par2set, name))

        # A deadlock holds the GIL, so the threads run in a process of their own that
        # can be given up on
        script = textwrap.dedent("""
            import os, sys, threading
            import sabctools

            basepath, names = sys.argv[1], sys.argv[2:]
            shared = sabctools.Par2Repairer(os.path.join(basepath, "rec.par2"), basepath=basepath)
            shared.load()

            seen = []
            shared.file_done_callback = lambda name, found, total: seen.append(name)
            shared.progress_callback = lambda stage, filename, percent: None

            # A failure in any thread fails the script
            failures = []
            threading.excepthook = lambda failure: failures.append(failure.exc_value)

            # One thread keeps asking what has been found while the others feed files in
            feeding = threading.Event()

            def read():
                while not feeding.is_set():
                    assert shared.missing_block_count >= 0

            reader = threading.Thread(target=read)
            feeders = [
                threading.Thread(target=shared.verify_file, args=(os.path.join(basepath, name),))
                for name in names
            ]
            reader.start()
            for feeder in feeders:
                feeder.start()
            for feeder in feeders:
                feeder.join()
            feeding.set()
            reader.join()

            if failures:
                sys.exit("thread failed: %r" % failures)
            print(sorted(seen) == sorted(names), shared.complete_file_count, shared.available_block_count)
            """)
        try:
            run = subprocess.run(
                [sys.executable, "-c", script, par2set, *names], capture_output=True, text=True, timeout=120
            )
        except subprocess.TimeoutExpired:
            pytest.fail("verify_file from several threads deadlocked")

        assert run.returncode == 0, run.stderr
        assert run.stdout.split() == ["True", str(alone.complete_file_count), str(alone.available_block_count)]
        assert alone.complete_file_count == 3

    def test_rescan_replaces_what_the_first_scan_found(self, par2set, digests):
        # right size, wrong contents: what a file still downloading looks like
        target = os.path.join(par2set, "gamma.bin")
        good = open(target, "rb").read()
        with open(target, "r+b") as f:
            f.seek(1000)
            f.write(b"\0" * 4000)

        rep = repairer(par2set)
        rep.load()
        rep.verify_file(target)
        assert rep.damaged_file_count == 1
        partial = rep.available_block_count

        # it finishes, and is scanned again
        with open(target, "wb") as f:
            f.write(good)
        rep.verify_file(target)
        assert rep.damaged_file_count == 0
        assert rep.complete_file_count == 1
        assert rep.available_block_count > partial

    def test_file_scanned_before_load_is_replayed(self, par2set):
        rep = repairer(par2set)
        # nothing describes it yet
        assert rep.verify_file(os.path.join(par2set, "alpha.bin")) == (sabctools.Par2Result.INSUFFICIENT_CRITICAL_DATA)
        # loading the packets scans it
        rep.load()
        assert rep.complete_file_count == 1

    def test_repair_works_from_incremental_scans(self, par2set, digests):
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        rep.load()
        for name in ("alpha.bin", "beta.bin"):
            rep.verify_file(os.path.join(par2set, name))

        assert rep.repair_possible
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        assert md5(os.path.join(par2set, "gamma.bin")) == digests["gamma.bin"]


class TestPar2LoadMore:
    """Adding recovery blocks to a live repairer, the 'fetch more blocks' path.

    par2 finds sibling rec.vol*.par2 files by name during load(), so these tests stage
    the volume file only after loading to keep it out of the initial set.
    """

    @staticmethod
    def without_volumes(par2set):
        volumes = [f for f in os.listdir(par2set) if "vol" in f]
        held = {}
        for name in volumes:
            path = os.path.join(par2set, name)
            with open(path, "rb") as f:
                held[name] = f.read()
            os.remove(path)
        return held

    def test_more_blocks_make_repair_possible(self, par2set, digests):
        held = self.without_volumes(par2set)
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        assert rep.load() == sabctools.Par2Result.SUCCESS
        assert rep.recovery_block_count == 0
        assert rep.verify() == sabctools.Par2Result.REPAIR_NOT_POSSIBLE
        assert not rep.repair_possible
        assert rep.missing_block_count == 320

        # SABnzbd would have fetched these in the meantime
        restored = []
        for name, data in held.items():
            path = os.path.join(par2set, name)
            with open(path, "wb") as f:
                f.write(data)
            restored.append(path)

        assert rep.load_more(restored) == RECOVERY_BLOCKS
        assert rep.repair_possible
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        assert md5(os.path.join(par2set, "gamma.bin")) == digests["gamma.bin"]

    def test_repair_after_load_more_does_not_reverify(self, par2set):
        held = self.without_volumes(par2set)
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)
        rep.load()
        rep.verify()

        restored = []
        for name, data in held.items():
            path = os.path.join(par2set, name)
            with open(path, "wb") as f:
                f.write(data)
            restored.append(path)
        rep.load_more(restored)

        # Only watch the repair; a re-verify would show up as "verifying" events
        stages = []
        rep.progress_callback = lambda stage, filename, percent: stages.append(stage)
        assert rep.repair() == sabctools.Par2Result.SUCCESS
        assert "verifying" not in stages
        assert "repairing" in stages

    def test_load_can_read_only_the_named_file(self, par2set):
        rep = repairer(par2set)
        assert rep.load(volumes=False) == sabctools.Par2Result.SUCCESS
        assert rep.recovery_block_count == 0
        assert rep.load_more([os.path.join(par2set, "rec.vol000+576.par2")]) == RECOVERY_BLOCKS

    def test_load_more_before_load_is_rejected(self, par2set):
        rep = repairer(par2set)
        with pytest.raises(RuntimeError):
            rep.load_more([])

    def test_load_more_is_idempotent(self, par2set):
        rep = repairer(par2set)
        rep.load()
        before = rep.recovery_block_count
        # Already pulled in by load(); loading it again must not double-count
        assert rep.load_more([os.path.join(par2set, "rec.vol000+576.par2")]) == before

    def test_load_more_rejects_a_missing_file(self, par2set):
        rep = repairer(par2set)
        rep.load()
        with pytest.raises(sabctools.Par2Error):
            rep.load_more([os.path.join(par2set, "does-not-exist.par2")])

    def test_load_more_reads_only_the_files_named(self, par2set):
        held = self.without_volumes(par2set)

        rep = repairer(par2set)
        rep.load()
        for name, data in held.items():
            with open(os.path.join(par2set, name), "wb") as f:
                f.write(data)

        with pytest.raises(sabctools.Par2Error):
            rep.load_more([os.path.join(par2set, "rec.vol000+576.par2.typo")])
        assert rep.recovery_block_count == 0


class TestPar2Renames:
    def test_obfuscated_file_is_matched(self, par2set):
        obfuscated = os.path.join(par2set, "abc123def456ghi789.tmp")
        os.rename(os.path.join(par2set, "beta.bin"), obfuscated)

        rep = repairer(par2set, extrafiles=[obfuscated])
        rep.load()
        rep.verify()

        assert rep.renamed_file_count == 1
        assert rep.renames == {obfuscated: os.path.join(par2set, "beta.bin")}

    def test_repair_applies_the_rename(self, par2set, digests):
        obfuscated = os.path.join(par2set, "abc123def456ghi789.tmp")
        os.rename(os.path.join(par2set, "beta.bin"), obfuscated)

        rep = repairer(par2set, extrafiles=[obfuscated])
        rep.load()
        rep.verify()
        assert rep.repair() == sabctools.Par2Result.SUCCESS

        assert not os.path.exists(obfuscated)
        assert md5(os.path.join(par2set, "beta.bin")) == digests["beta.bin"]


class TestPar2Progress:
    def test_stages_and_percentages(self, par2set):
        os.remove(os.path.join(par2set, "gamma.bin"))

        events = []
        rep = repairer(par2set)
        rep.progress_callback = lambda stage, filename, percent: events.append((stage, filename, percent))
        rep.load()
        rep.verify()
        rep.repair()

        # The stages arrive in the order par2 works in, each counting from zero again
        order = []
        for stage, _, _ in events:
            if not order or order[-1] != stage:
                order.append(stage)
        assert order == [
            "loading",
            "verifying",
            "repairing",
            "verifying_repair",
        ]
        for stage, _, percent in events:
            assert 0 <= percent <= 100, (stage, percent)

        # Loading names each par2 file it opens, and reports that file's percentage
        # under an empty name
        assert {os.path.basename(f) for stage, f, _ in events if stage == "loading" and f} == {
            "rec.par2",
            "rec.vol000+576.par2",
        }

    def test_callback_can_be_cleared(self, par2set):
        rep = repairer(par2set)
        rep.progress_callback = lambda *a: None
        assert rep.progress_callback is not None
        rep.progress_callback = None
        assert rep.progress_callback is None
        rep.load()
        assert rep.verify() == sabctools.Par2Result.SUCCESS

    def test_non_callable_is_rejected(self, par2set):
        rep = repairer(par2set)
        with pytest.raises(TypeError):
            rep.progress_callback = "not callable"

    @pytest.mark.filterwarnings("ignore::pytest.PytestUnraisableExceptionWarning")
    def test_callback_exception_does_not_break_the_run(self, par2set):
        def boom(stage, filename, percent):
            raise RuntimeError("callback blew up")

        rep = repairer(par2set)
        rep.progress_callback = boom
        rep.load()
        # Exceptions cannot unwind through par2's C++ frames, so they are reported
        # as unraisable and the run continues
        assert rep.verify() == sabctools.Par2Result.SUCCESS

    def test_the_set_can_be_read_from_the_callbacks(self, par2set):
        os.remove(os.path.join(par2set, "gamma.bin"))

        rep = repairer(par2set)

        def read_set():
            return (
                rep.recoverable_file_count,
                rep.source_block_count,
                rep.recovery_block_count,
                rep.block_size,
                rep.data_size,
                rep.setid,
                rep.creator,
            )

        rep.load()
        expected = read_set()
        seen = []
        rep.progress_callback = lambda *args: seen.append(read_set())
        rep.file_done_callback = lambda *args: seen.append(read_set())

        results = []
        worker = threading.Thread(target=lambda: results.extend([rep.verify(), rep.repair()]), daemon=True)
        worker.start()
        worker.join(timeout=60)

        assert not worker.is_alive()
        assert results == [sabctools.Par2Result.REPAIR_POSSIBLE, sabctools.Par2Result.SUCCESS]
        assert expected[0] == 3
        assert seen and set(seen) == {expected}


class TestPar2Errors:
    def test_a_clean_run_reports_nothing(self, par2set):
        rep = repairer(par2set)
        rep.load()
        assert rep.verify() == sabctools.Par2Result.SUCCESS
        assert rep.last_error is None

    def test_a_missing_set_says_which_file_and_why(self, tmp_path):
        missing = os.path.join(str(tmp_path), "nope.par2")
        rep = sabctools.Par2Repairer(missing, basepath=str(tmp_path))
        assert rep.load() == sabctools.Par2Result.FILE_IO_ERROR

        error = rep.last_error
        assert error["code"] == sabctools.Par2ErrorCode.PAR2_FILE_MISSING
        assert error["filename"] == missing
        assert error["message"]

    def test_the_callback_sees_it_as_it_happens(self, tmp_path):
        errors = []
        rep = sabctools.Par2Repairer(os.path.join(str(tmp_path), "nope.par2"), basepath=str(tmp_path))
        rep.error_callback = lambda code, message, filename: errors.append(code)
        rep.load()

        assert errors == [sabctools.Par2ErrorCode.PAR2_FILE_MISSING]

    def test_callbacks_can_be_cleared(self, par2set):
        rep = repairer(par2set)
        rep.error_callback = lambda *a: None
        rep.warning_callback = lambda *a: None
        assert rep.error_callback is not None
        assert rep.warning_callback is not None
        rep.error_callback = None
        rep.warning_callback = None
        assert rep.error_callback is None
        assert rep.warning_callback is None

    def test_a_callback_may_clear_itself_while_it_runs(self, par2set):
        os.remove(os.path.join(par2set, "gamma.bin"))
        rep = repairer(par2set)
        calls = []

        def once(stage, filename, percent):
            calls.append(stage)
            rep.progress_callback = None

        rep.progress_callback = once
        del once
        rep.load()
        rep.verify()
        assert calls == ["loading"]

    @staticmethod
    def make_cycle(par2set, marker):
        rep = repairer(par2set)
        rep.progress_callback = lambda *args: (rep, marker)
        rep.file_done_callback = rep.progress_callback
        rep.error_callback = rep.progress_callback
        rep.warning_callback = rep.progress_callback

    def test_a_repairer_its_callbacks_refer_to_is_collected(self, par2set):
        collected = threading.Event()

        class Marker:
            def __del__(self):
                collected.set()

        self.make_cycle(par2set, Marker())
        gc.collect()
        assert collected.is_set()

    def test_non_callable_is_rejected(self, par2set):
        rep = repairer(par2set)
        with pytest.raises(TypeError):
            rep.error_callback = "not callable"
        with pytest.raises(TypeError):
            rep.warning_callback = "not callable"


class TestPar2Cancel:
    def test_cancel_from_callback(self, par2set):
        rep = repairer(par2set)
        rep.load()

        def cancel_once(stage, filename, percent):
            if stage == "verifying":
                rep.cancel()

        rep.progress_callback = cancel_once
        rep.verify()
        assert rep.cancelled

    def test_cancel_from_another_thread(self, par2set):
        rep = repairer(par2set)
        rep.load()

        started = threading.Event()
        rep.progress_callback = lambda *a: started.set()

        def canceller():
            started.wait(timeout=10)
            rep.cancel()

        thread = threading.Thread(target=canceller)
        thread.start()
        rep.verify()
        thread.join()
        assert rep.cancelled

    def test_cancel_before_running_is_harmless(self, par2set):
        rep = repairer(par2set)
        rep.cancel()
        assert rep.cancelled
