/*
 * Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "par2.h"
#include "par2_parpar.h"

#include <par2/libpar2.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

/*
 * Drives par2cmdline as a library so that callers get typed results
 * instead of the repair tool's prose.
 *
 * Everything here goes through par2/libpar2.h and nothing else: Par2Verifier is a
 * pimpl and Par2Observer is the callback interface, so none of par2's internal
 * headers are included and none of its members are read.
 *
 * The sequence is load() -> set_known_blocks() -> verify() -> repair(), and a
 * verifier stays usable afterwards: load_more() adds recovery blocks that arrived
 * late, and repair_possible then says whether they are enough without the data
 * files being read again.
 */

static PyObject* Par2Error = NULL;

/* The IntEnums the module exposes, held so that a value can be returned as a member
   of one rather than as the bare int behind it. */
static PyObject* Par2ResultType = NULL;
static PyObject* Par2ErrorCodeType = NULL;
static PyObject* Par2WarningCodeType = NULL;

static PyObject* enum_value(PyObject* type, int value) {
    return PyObject_CallFunction(type, "i", value);
}

/* A name is the bytes par2 holds, which need not be UTF-8. Bytes which are not
   become lone surrogates, as os.fsdecode() gives them, and go back unchanged. */
static PyObject* name_to_python(const std::string& name) {
    return PyUnicode_DecodeUTF8(name.data(), (Py_ssize_t)name.size(), "surrogateescape");
}

/* A message is display text which may quote such a name, so bytes which are not UTF-8
   are replaced. */
static PyObject* message_to_python(const std::string& message) {
    return PyUnicode_DecodeUTF8(message.data(), (Py_ssize_t)message.size(), "replace");
}

static bool name_from_python(PyObject* object, std::string* name) {
    PyObject* bytes = PyUnicode_AsEncodedString(object, "utf-8", "surrogateescape");
    if (bytes == NULL)
        return false;
    name->assign(PyBytes_AS_STRING(bytes), (size_t)PyBytes_GET_SIZE(bytes));
    Py_DECREF(bytes);
    return true;
}

/* name_from_python for PyArg_ParseTupleAndKeywords's O& */
static int name_converter(PyObject* object, void* name) {
    return name_from_python(object, (std::string*)name) ? 1 : 0;
}

static const char* const STAGE_LOADING = "loading";
static const char* const STAGE_VERIFYING = "verifying";
static const char* const STAGE_REPAIRING = "repairing";
static const char* const STAGE_VERIFYING_REPAIR = "verifying_repair";

struct Par2RepairerObject;

/* Relays par2's progress and per-file results to the Python callbacks. */
class SabObserver final : public par2::Par2Observer {
public:
    SabObserver() : owner(NULL) {}

    void SetOwner(Par2RepairerObject* o) { owner = o; }

    void OnFile(par2::Phase phase, const std::string& filename) override;
    void OnProgress(par2::Phase phase, par2::u32 permille) override;
    void OnFileDone(par2::Phase phase, const par2::Par2FileResult& file) override;
    void OnError(const par2::Par2Error& error) override;
    void OnWarning(const par2::Par2Warning& warning) override;

private:
    Par2RepairerObject* owner;
};

typedef struct Par2RepairerObject {
    PyObject_HEAD
    par2::Par2Verifier* verifier;
    SabObserver* observer;

    PyObject* progress_callback;
    PyObject* file_done_callback;
    PyObject* error_callback;
    PyObject* warning_callback;

    /* Verify takes these, so they are held from construction until it runs. */
    std::string* parfile;
    std::vector<std::string>* extrafiles;
    bool skip_data;
    unsigned long long skip_leaway;

    bool skip_repaired_verification;

    /* The set as of the last par2 file read, NULL before one has been. Guarded by
       the GIL. */
    par2::Par2SetInfo* setinfo;

    /* The files the last repair() repaired in place, a list guarded by the GIL. */
    PyObject* in_place_files;

    bool loaded;
    bool verified;
    bool cancelled;

    const char* stage;
    /* Last value passed to the callback */
    int last_progress;
    /* Last value par2 gave, which only rises within a step. */
    long last_permille;
} Par2RepairerObject;

/*
 * Invoke the user's progress callback. Called from par2's worker threads as well
 * as the calling thread, so the GIL has to be taken explicitly.
 *
 * par2 holds a lock across some of these calls, so the callback must stay short
 * and must not re-enter the verifier. An exception raised by the callback is
 * printed as unraisable rather than propagated through par2's C++ frames.
 */
static void call_progress(Par2RepairerObject* self, const char* stage,
                          const std::string& filename, int progress) {
    if (!self)
        return;

    PyGILState_STATE gstate = PyGILState_Ensure();

    /* Read with the GIL held, and kept alive for the call */
    PyObject* callback = self->progress_callback;
    if (callback) {
        Py_INCREF(callback);
        PyObject* result = PyObject_CallFunction(callback, "sNi", stage, name_to_python(filename), progress);
        if (result == NULL) {
            PyErr_WriteUnraisable(callback);
        } else {
            Py_DECREF(result);
        }
        Py_DECREF(callback);
    }

    PyGILState_Release(gstate);
}

/* par2 says which step a count belongs to; the caller is told in its own terms. */
static const char* stage_of(par2::Phase phase) {
    switch (phase) {
        case par2::phLoading:
            return STAGE_LOADING;
        case par2::phScanning:
            return STAGE_VERIFYING;
        case par2::phVerifyingRepair:
            return STAGE_VERIFYING_REPAIR;
        default:
            return STAGE_REPAIRING;
    }
}

void SabObserver::OnFile(par2::Phase phase, const std::string& filename) {
    if (!owner || phase == par2::phWriting)
        return;
    call_progress(owner, stage_of(phase), filename, 0);
}

void SabObserver::OnProgress(par2::Phase phase, par2::u32 permille) {
    if (!owner)
        return;

    /* A step, or another run of the same step, starts the thinning below again. */
    const char* stage = stage_of(phase);
    if (stage != owner->stage || (long)permille <= owner->last_permille) {
        owner->stage = stage;
        owner->last_progress = -1;
    }
    owner->last_permille = (long)permille;

    /* Only whole percents reach the callback */
    int percent = (int)(permille / 10);
    if (percent == owner->last_progress)
        return;
    owner->last_progress = percent;

    call_progress(owner, owner->stage, std::string(), percent);
}

/*
 * Fires once per file par2 finishes with, saying how many of that file's blocks it
 * could use. Both counts are zero for a par2 file, which has no blocks of its own.
 * The files a repair wrote are not reported.
 */
void SabObserver::OnFileDone(par2::Phase phase, const par2::Par2FileResult& file) {
    if (owner && phase == par2::phWriting && file.inplace) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* name = name_to_python(file.filename);
        if (name == NULL || owner->in_place_files == NULL || PyList_Append(owner->in_place_files, name) < 0)
            PyErr_WriteUnraisable(NULL);
        Py_XDECREF(name);
        PyGILState_Release(gstate);
    }

    if (!owner || phase == par2::phWriting)
        return;

    const unsigned int found = phase == par2::phLoading ? 0 : (unsigned int)file.blocksfound;

    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* callback = owner->file_done_callback;
    if (callback) {
        Py_INCREF(callback);
        PyObject* result = PyObject_CallFunction(callback, "NII", name_to_python(file.filename), found,
                                                 (unsigned int)file.blocksneeded);
        if (result == NULL) {
            PyErr_WriteUnraisable(callback);
        } else {
            Py_DECREF(result);
        }
        Py_DECREF(callback);
    }
    PyGILState_Release(gstate);
}

/*
 * Relay one (code, message, filename) report to the callback in slot. Called from the
 * thread that found the thing, under the same rules as call_progress.
 */
static void call_report(PyObject* const* slot, PyObject* type, int code,
                        const std::string& message, const std::string& filename) {
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* callback = *slot;
    if (callback) {
        Py_INCREF(callback);
        PyObject* result = PyObject_CallFunction(callback, "NNN", enum_value(type, code),
                                                 message_to_python(message), name_to_python(filename));
        if (result == NULL) {
            PyErr_WriteUnraisable(callback);
        } else {
            Py_DECREF(result);
        }
        Py_DECREF(callback);
    }

    PyGILState_Release(gstate);
}

/*
 * Fires once per error as par2 finds it. The operation may carry on and may still
 * succeed - a file it could not open is not a failed verify - so this reports what
 * went wrong on the way, and the Par2Result says whether it mattered.
 */
void SabObserver::OnError(const par2::Par2Error& error) {
    if (!owner)
        return;
    call_report(&owner->error_callback, Par2ErrorCodeType, (int)error.code, error.message,
                error.filename);
}

/*
 * Fires once per warning as par2 finds it, most often a name the set records which
 * this system will not take as it stands.
 */
void SabObserver::OnWarning(const par2::Par2Warning& warning) {
    if (!owner)
        return;
    call_report(&owner->warning_callback, Par2WarningCodeType, (int)warning.code, warning.message,
                warning.filename);
}

/* ------------------------------------------------------------------------- */

/*
 * Run a libpar2 call with the GIL released, turning a C++ exception into Par2Error.
 * Returns false with a Python exception set.
 */
template <typename Call>
static bool run_step(Call call, par2::Result* result) {
    bool threw = false;
    std::string message;

    Py_BEGIN_ALLOW_THREADS
    try {
        *result = call();
    } catch (const std::exception& e) {
        threw = true;
        message = e.what();
    } catch (...) {
        threw = true;
        message = "unknown error in par2";
    }
    Py_END_ALLOW_THREADS

    if (threw) {
        PyErr_SetString(Par2Error, message.c_str());
        return false;
    }
    return true;
}

/* The GIL released for as long as this is in scope. */
struct GilReleased {
    GilReleased() : state(PyEval_SaveThread()) {}
    ~GilReleased() { PyEval_RestoreThread(state); }
    PyThreadState* state;
};

/*
 * Run a call into the verifier with the GIL released. A verifier call waits for any
 * other call on it to finish, and that call's callbacks need the GIL.
 */
template <typename Call>
static auto without_gil(Call call) -> decltype(call()) {
    GilReleased released;
    return call();
}

static PyObject* Par2Repairer_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    (void)args;
    (void)kwds;
    Par2RepairerObject* self = (Par2RepairerObject*)type->tp_alloc(type, 0);
    if (self == NULL)
        return NULL;

    self->verifier = NULL;
    self->observer = NULL;
    self->progress_callback = NULL;
    self->file_done_callback = NULL;
    self->error_callback = NULL;
    self->warning_callback = NULL;
    self->parfile = NULL;
    self->extrafiles = NULL;
    self->skip_data = false;
    self->skip_leaway = 0;
    self->skip_repaired_verification = true;
    self->setinfo = NULL;
    self->in_place_files = NULL;
    self->loaded = false;
    self->verified = false;
    self->cancelled = false;
    self->stage = STAGE_LOADING;
    self->last_progress = -1;
    self->last_permille = -1;
    return (PyObject*)self;
}

static int Par2Repairer_traverse(Par2RepairerObject* self, visitproc visit, void* arg) {
    Py_VISIT(self->in_place_files);
    Py_VISIT(self->progress_callback);
    Py_VISIT(self->file_done_callback);
    Py_VISIT(self->error_callback);
    Py_VISIT(self->warning_callback);
    return 0;
}

static int Par2Repairer_clear(Par2RepairerObject* self) {
    Py_CLEAR(self->in_place_files);
    Py_CLEAR(self->progress_callback);
    Py_CLEAR(self->file_done_callback);
    Py_CLEAR(self->error_callback);
    Py_CLEAR(self->warning_callback);
    return 0;
}

static void Par2Repairer_dealloc(Par2RepairerObject* self) {
    PyObject_GC_UnTrack(self);
    /* Before the observer, which the verifier holds a pointer to. */
    delete self->verifier;
    delete self->observer;
    delete self->parfile;
    delete self->extrafiles;
    delete self->setinfo;
    Py_XDECREF(self->in_place_files);
    Py_XDECREF(self->progress_callback);
    Py_XDECREF(self->file_done_callback);
    Py_XDECREF(self->error_callback);
    Py_XDECREF(self->warning_callback);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int Par2Repairer_init(Par2RepairerObject* self, PyObject* args, PyObject* kwds) {
    static const char* kwlist[] = {"parfile",     "extrafiles",  "basepath",
                                   "memory_limit", "threads",     "file_threads",
                                   "skip_data",    "skip_leaway",
                                   "skip_repaired_verification", "in_place_repair", NULL};

    std::string parfile;
    PyObject* extrafiles = NULL;
    std::string basepath;
    unsigned long long memory_limit = 0;
    unsigned int threads = 0;
    unsigned int file_threads = 0;
    int skip_data = 0;
    unsigned long long skip_leaway = 0;
    int skip_repaired_verification = 1;
    int in_place_repair = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&|OO&KIIpKpp", (char**)kwlist,
                                     name_converter, &parfile, &extrafiles, name_converter,
                                     &basepath, &memory_limit, &threads,
                                     &file_threads, &skip_data, &skip_leaway,
                                     &skip_repaired_verification, &in_place_repair))
        return -1;

    std::vector<std::string> extras;
    if (extrafiles && extrafiles != Py_None) {
        PyObject* seq = PySequence_Fast(extrafiles, "extrafiles must be a sequence of str");
        if (seq == NULL)
            return -1;
        Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
        for (Py_ssize_t i = 0; i < count; i++) {
            std::string path;
            if (!name_from_python(PySequence_Fast_GET_ITEM(seq, i), &path)) {
                Py_DECREF(seq);
                return -1;
            }
            extras.push_back(path);
        }
        Py_DECREF(seq);
    }

    delete self->verifier;
    delete self->observer;
    delete self->parfile;
    delete self->extrafiles;
    delete self->setinfo;
    self->verifier = NULL;
    self->observer = NULL;
    self->parfile = NULL;
    self->extrafiles = NULL;
    self->setinfo = NULL;
    Py_CLEAR(self->in_place_files);

    self->parfile = new std::string(parfile);
    self->extrafiles = new std::vector<std::string>(extras);
    self->skip_data = skip_data != 0;
    self->skip_leaway = skip_leaway;
    self->skip_repaired_verification = skip_repaired_verification != 0;
    self->loaded = false;
    self->verified = false;
    self->cancelled = false;
    self->stage = STAGE_LOADING;
    self->last_progress = -1;
    self->last_permille = -1;

    /* An empty basepath is taken from the first par2 file added, and a memory
       limit of zero lets par2 size itself from physical memory. */
    self->verifier = new par2::Par2Verifier(basepath, ParParBackends());
    self->verifier->SetMemoryLimit((size_t)memory_limit);
    self->verifier->SetThreadCounts(threads, file_threads);
    self->verifier->SetDataSkipping(self->skip_data, (par2::u64)self->skip_leaway);
    self->verifier->SetInPlaceRepair(in_place_repair != 0);

    self->observer = new SabObserver();
    self->observer->SetOwner(self);
    self->verifier->SetObserver(self->observer);
    return 0;
}

/* Take a new snapshot of the set after par2 files have been read. */
static void refresh_set_info(Par2RepairerObject* self) {
    par2::Par2SetInfo info;
    par2::Par2Verifier* verifier = self->verifier;
    if (!without_gil([&] { return verifier->GetSetInfo(&info); }))
        return;
    if (self->setinfo == NULL)
        self->setinfo = new par2::Par2SetInfo(info);
    else
        *self->setinfo = info;
}

static bool ready(Par2RepairerObject* self) {
    if (self->verifier == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Par2Repairer is not initialised");
        return false;
    }
    return true;
}

static bool ready_and_loaded(Par2RepairerObject* self, const char* what) {
    if (!ready(self))
        return false;
    if (!self->loaded) {
        PyErr_Format(PyExc_RuntimeError, "call load() before %s()", what);
        return false;
    }
    return true;
}

static PyObject* Par2Repairer_load(Par2RepairerObject* self, PyObject* args, PyObject* kwds) {
    static const char* kwlist[] = {"volumes", NULL};
    int volumes = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|p", (char**)kwlist, &volumes))
        return NULL;
    if (!ready(self))
        return NULL;

    par2::Result result;
    par2::Par2Verifier* verifier = self->verifier;
    const std::string parfile = *self->parfile;
    const bool ran = run_step([&] { return verifier->AddPar2File(parfile, volumes != 0); }, &result);
    refresh_set_info(self);
    if (!ran)
        return NULL;

    if (result == par2::eSuccess)
        self->loaded = true;
    if (result == par2::eCancelled)
        self->cancelled = true;

    return enum_value(Par2ResultType, (int)result);
}

/* Whether a verify ran to its end, which is what a repair builds on */
static bool verify_finished(par2::Result result) {
    return result == par2::eSuccess || result == par2::eRepairPossible || result == par2::eRepairNotPossible;
}

/* Scan the source files, leaving the outcome in self. False with a Python exception
   set. */
static bool do_verify(Par2RepairerObject* self, par2::Result* result) {
    par2::Par2Verifier* verifier = self->verifier;
    const std::vector<std::string>& extras = *self->extrafiles;
    if (!run_step([&] { return verifier->Verify(extras); }, result))
        return false;

    if (*result == par2::eCancelled)
        self->cancelled = true;
    else if (verify_finished(*result))
        self->verified = true;
    return true;
}

static PyObject* Par2Repairer_verify(Par2RepairerObject* self, PyObject* Py_UNUSED(ignored)) {
    if (!ready_and_loaded(self, "verify"))
        return NULL;

    par2::Result result;
    if (!do_verify(self, &result))
        return NULL;
    return enum_value(Par2ResultType, (int)result);
}

/*
 * Scan one file as it becomes available, rather than waiting for the whole set.
 *
 * Nothing else is read, so files which are on disk at their final size but still
 * downloading are not scanned until the caller says they are finished. Calling it
 * again for the same file discards what the earlier scan of it found.
 */
static PyObject* Par2Repairer_verify_file(Par2RepairerObject* self, PyObject* arg) {
    if (!ready(self))
        return NULL;

    std::string path;
    if (!name_from_python(arg, &path))
        return NULL;

    par2::Result result;
    par2::Par2Verifier* verifier = self->verifier;
    if (!run_step([&] { return verifier->VerifyFile(path); }, &result))
        return NULL;

    if (result == par2::eCancelled)
        self->cancelled = true;
    else if (verify_finished(result))
        self->verified = true;

    return enum_value(Par2ResultType, (int)result);
}

/*
 * The CRC32 the set records for each block of one file, which is what the par2 packets
 * say the data should be rather than anything read off disk. None when the set does not
 * describe that file, which includes a file it holds no verification packet for and so
 * cannot recover.
 */
static PyObject* Par2Repairer_block_checksums(Par2RepairerObject* self, PyObject* arg) {
    if (!ready_and_loaded(self, "block_checksums"))
        return NULL;

    std::string filename;
    if (!name_from_python(arg, &filename))
        return NULL;

    std::vector<par2::u32> crcs;
    if (!without_gil([&] { return self->verifier->GetBlockChecksums(filename, &crcs); }))
        Py_RETURN_NONE;

    PyObject* list = PyList_New((Py_ssize_t)crcs.size());
    if (list == NULL)
        return NULL;

    for (size_t i = 0; i < crcs.size(); i++) {
        PyObject* value = PyLong_FromUnsignedLong((unsigned long)crcs[i]);
        if (value == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, (Py_ssize_t)i, value);
    }
    return list;
}

static PyObject* Par2Repairer_repair(Par2RepairerObject* self, PyObject* Py_UNUSED(ignored)) {
    if (!ready_and_loaded(self, "repair"))
        return NULL;

    /* Verify first where nothing has been verified yet */
    if (!self->verified) {
        par2::Result verified;
        if (!do_verify(self, &verified))
            return NULL;
        if (verified == par2::eCancelled || verified == par2::eRepairNotPossible)
            return enum_value(Par2ResultType, (int)verified);
    }

    /* par2's pass over the files it rebuilt is skipped when
       skip_repaired_verification is set and set_known_blocks() said a block is present */
    bool vouched = false;
    if (self->skip_repaired_verification) {
        const std::map<std::string, std::vector<par2::KnownBlock> > known =
            without_gil([&] { return self->verifier->GetKnownBlocks(); });
        for (std::map<std::string, std::vector<par2::KnownBlock> >::const_iterator it = known.begin();
             it != known.end() && !vouched; ++it)
            vouched = std::find(it->second.begin(), it->second.end(), par2::kbPresent) != it->second.end();
    }
    const bool verifyafter = !vouched;

    PyObject* in_place_files = PyList_New(0);
    if (in_place_files == NULL)
        return NULL;
    Py_XSETREF(self->in_place_files, in_place_files);

    par2::Result result;
    par2::Par2Verifier* verifier = self->verifier;
    if (!run_step([&] { return verifier->Repair(verifyafter); }, &result))
        return NULL;

    if (result == par2::eCancelled)
        self->cancelled = true;

    return enum_value(Par2ResultType, (int)result);
}

/*
 * Add recovery blocks from further par2 files to an already-loaded verifier.
 *
 * The verification pass is not repeated: repair_possible then says whether what the
 * last verify found can now be repaired, without reading the data files again.
 */
static PyObject* Par2Repairer_load_more(Par2RepairerObject* self, PyObject* parfiles) {
    if (!ready_and_loaded(self, "load_more"))
        return NULL;

    PyObject* sequence = PySequence_Fast(parfiles, "load_more() takes a sequence of str");
    if (sequence == NULL)
        return NULL;

    std::vector<std::string> paths;
    Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
    for (Py_ssize_t i = 0; i < count; i++) {
        std::string path;
        if (!name_from_python(PySequence_Fast_GET_ITEM(sequence, i), &path)) {
            Py_DECREF(sequence);
            return NULL;
        }
        paths.push_back(path);
    }
    Py_DECREF(sequence);

    par2::Par2Verifier* verifier = self->verifier;
    for (size_t i = 0; i < paths.size(); i++) {
        par2::Result result;
        const std::string& path = paths[i];
        const bool ran = run_step([&] { return verifier->AddPar2File(path, false); }, &result);
        if (!ran || result == par2::eFileIOError)
            refresh_set_info(self);
        if (!ran)
            return NULL;
        /* The named file does not exist or could not be opened. */
        if (result == par2::eFileIOError) {
            PyErr_Format(Par2Error, "par2 could not read: %s", path.c_str());
            return NULL;
        }
    }

    refresh_set_info(self);
    return PyLong_FromUnsignedLong(self->setinfo ? self->setinfo->recoveryblockcount : 0);
}

/*
 * Tell the verifier which blocks the caller already knows to be intact.
 *
 * Takes {filename: sequence of per-block truth values}, where filename is the name
 * as recorded in the par2 set and the sequence runs from block 0. Listed files are
 * not read or hashed during verify(); everything else is scanned normally, so a
 * partial map is fine and an empty one restores the default behaviour.
 *
 * Trust is the caller's to give: a block marked good here is taken at its word.
 * Call after load(), which is when the file names and block_size become known, and
 * before verify().
 */
static PyObject* Par2Repairer_set_known_blocks(Par2RepairerObject* self, PyObject* mapping) {
    if (!ready_and_loaded(self, "set_known_blocks"))
        return NULL;
    if (!PyDict_Check(mapping)) {
        PyErr_SetString(PyExc_TypeError, "set_known_blocks() takes a dict of {filename: blocks}");
        return NULL;
    }

    std::map<std::string, std::vector<par2::KnownBlock> > vouched;

    PyObject *key, *value;
    Py_ssize_t position = 0;
    while (PyDict_Next(mapping, &position, &key, &value)) {
        std::string filename;
        if (!name_from_python(key, &filename))
            return NULL;

        PyObject* sequence = PySequence_Fast(value, "block values must be a sequence");
        if (sequence == NULL)
            return NULL;

        Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
        std::vector<par2::KnownBlock> blocks;
        blocks.reserve((size_t)count);
        for (Py_ssize_t i = 0; i < count; i++) {
            PyObject* item = PySequence_Fast_GET_ITEM(sequence, i);
            if (item == Py_None) {
                blocks.push_back(par2::kbUnknown);
                continue;
            }
            int good = PyObject_IsTrue(item);
            if (good < 0) {
                Py_DECREF(sequence);
                return NULL;
            }
            blocks.push_back(good ? par2::kbPresent : par2::kbMissing);
        }
        Py_DECREF(sequence);

        vouched[filename] = blocks;
    }

    /* An empty vector is how libpar2 forgets what was said about a file, so retract
       anything the previous call named and this one does not. */
    without_gil([&] {
        const std::map<std::string, std::vector<par2::KnownBlock> > known = self->verifier->GetKnownBlocks();
        for (std::map<std::string, std::vector<par2::KnownBlock> >::const_iterator it = known.begin();
             it != known.end(); ++it) {
            if (vouched.find(it->first) == vouched.end())
                self->verifier->SetKnownBlocks(it->first, std::vector<par2::KnownBlock>());
        }

        for (std::map<std::string, std::vector<par2::KnownBlock> >::const_iterator it = vouched.begin();
             it != vouched.end(); ++it) {
            self->verifier->SetKnownBlocks(it->first, it->second);
        }
    });

    Py_RETURN_NONE;
}

static PyObject* Par2Repairer_cancel(Par2RepairerObject* self, PyObject* Py_UNUSED(ignored)) {
    if (self->verifier)
        self->verifier->Cancel();
    self->cancelled = true;
    Py_RETURN_NONE;
}

static PyMethodDef Par2Repairer_methods[] = {
    {"load", (PyCFunction)(void (*)(void))Par2Repairer_load, METH_VARARGS | METH_KEYWORDS,
     "load(volumes=True) -> Par2Result\n\nRead the par2 packets and work out the file set. The\n"
     "volume files beside the named one are read too unless volumes is False, so naming a\n"
     "set whose index file is missing still describes it."},
    {"load_more", (PyCFunction)Par2Repairer_load_more, METH_O,
     "load_more(parfiles) -> int\n\nAdd recovery blocks from further par2 files and return the\n"
     "new recovery_block_count. Does not re-scan the data files: once verify() has run,\n"
     "repair_possible says whether there are now enough blocks to repair."},
    {"set_known_blocks", (PyCFunction)Par2Repairer_set_known_blocks, METH_O,
     "set_known_blocks(mapping)\n\nTake {filename: per-block truth values} as already\n"
     "verified. Those files are not read or hashed during verify(). Call after load()."},
    {"verify_file", (PyCFunction)Par2Repairer_verify_file, METH_O,
     "verify_file(filename) -> Par2Result\n\nScan one file as it becomes available,\n"
     "reading nothing else. Call it again for the same file once it has finished\n"
     "downloading; what an earlier scan found for it is discarded first. May be called\n"
     "before load(), which returns INSUFFICIENT_CRITICAL_DATA and scans the file once\n"
     "the par2 packets arrive."},
    {"block_checksums", (PyCFunction)Par2Repairer_block_checksums, METH_O,
     "block_checksums(filename) -> list[int] | None\n\nThe CRC32 the set records for each\n"
     "block of that file, one entry per block from block 0. None when the set does not\n"
     "describe the file. Requires load() first."},
    {"verify", (PyCFunction)Par2Repairer_verify, METH_NOARGS,
     "verify() -> Par2Result\n\nScan the source files. Requires load() first. May be called\n"
     "more than once; each call is a fresh pass."},
    {"repair", (PyCFunction)Par2Repairer_repair, METH_NOARGS,
     "repair() -> Par2Result\n\nRebuild whatever the preceding verify() found to be missing\n"
     "or damaged."},
    {"cancel", (PyCFunction)Par2Repairer_cancel, METH_NOARGS,
     "cancel()\n\nAsk an in-progress verify() or repair() to stop. Safe to call from another\n"
     "thread, including from progress_callback. The interrupted call returns\n"
     "Par2Result.CANCELLED, having removed any partly written files."},
    {NULL, NULL, 0, NULL}};

/* ------------------------------------------------------------------------- */

static bool set_info(Par2RepairerObject* self, par2::Par2SetInfo* info) {
    if (self->setinfo == NULL)
        return false;
    *info = *self->setinfo;
    return true;
}

static bool verify_result(Par2RepairerObject* self, par2::Par2VerifyResult* result) {
    return self->verifier != NULL &&
           without_gil([&] { return self->verifier->GetVerifyResult(result); });
}

static PyObject* get_missing_block_count(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    return PyLong_FromUnsignedLong(verify_result(self, &result) ? result.missingblockcount : 0);
}

static PyObject* get_available_block_count(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    return PyLong_FromUnsignedLong(verify_result(self, &result) ? result.availableblockcount : 0);
}

static PyObject* get_source_block_count(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    return PyLong_FromUnsignedLong(set_info(self, &info) ? info.datablockcount : 0);
}

/* From the set rather than the verify result, which is empty until a scan has run.
   The two agree once one has. */
static PyObject* get_recovery_block_count(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    return PyLong_FromUnsignedLong(set_info(self, &info) ? info.recoveryblockcount : 0);
}

static PyObject* get_recoverable_file_count(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    return PyLong_FromUnsignedLong(set_info(self, &info) ? info.recoverablefilecount : 0);
}

static PyObject* get_complete_file_count(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    return PyLong_FromUnsignedLong(verify_result(self, &result) ? result.completefilecount : 0);
}

static PyObject* get_damaged_file_count(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    return PyLong_FromUnsignedLong(verify_result(self, &result) ? result.damagedfilecount : 0);
}

static PyObject* get_missing_file_count(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    return PyLong_FromUnsignedLong(verify_result(self, &result) ? result.missingfilecount : 0);
}

static PyObject* get_renamed_file_count(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    return PyLong_FromUnsignedLong(verify_result(self, &result) ? result.renamedfilecount : 0);
}

static PyObject* get_block_size(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    return PyLong_FromUnsignedLongLong(set_info(self, &info) ? info.blocksize : 0);
}

static PyObject* get_data_size(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    return PyLong_FromUnsignedLongLong(set_info(self, &info) ? info.datasize : 0);
}

static PyObject* get_setid(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    if (!set_info(self, &info))
        return PyBytes_FromStringAndSize(NULL, 0);
    return PyBytes_FromStringAndSize((const char*)info.setid.data(), 16);
}

/*
 * The client that created the set, as its creator packet records it. That is free
 * text in whatever the client chose to write, so bytes which are not UTF-8 are
 * replaced rather than raised.
 */
static PyObject* get_creator(Par2RepairerObject* self, void*) {
    par2::Par2SetInfo info;
    if (!set_info(self, &info))
        return PyUnicode_FromString("");
    return PyUnicode_DecodeUTF8(info.creator.data(), (Py_ssize_t)info.creator.size(), "replace");
}

static PyObject* get_quick_verified_files(Par2RepairerObject* self, void*) {
    if (self->verifier == NULL)
        return PyLong_FromSize_t(0);
    return PyLong_FromSize_t(without_gil([&] { return self->verifier->GetKnownBlocks(); }).size());
}

static PyObject* get_cancelled(Par2RepairerObject* self, void*) {
    return PyBool_FromLong(self->cancelled);
}

/* True when there is enough recovery data to rebuild what is missing */
static PyObject* get_repair_possible(Par2RepairerObject* self, void*) {
    par2::Par2VerifyResult result;
    if (!verify_result(self, &result))
        Py_RETURN_FALSE;
    return PyBool_FromLong(result.recoveryblockcount >= result.missingblockcount);
}

static PyObject* get_in_place_files(Par2RepairerObject* self, void*) {
    if (self->in_place_files == NULL)
        return PyList_New(0);
    return PyList_GetSlice(self->in_place_files, 0, PY_SSIZE_T_MAX);
}

/*
 * The files a verify found under a name other than the one the set records, as
 * {path_on_disk: path_it_belongs_under}. Both paths are absolute.
 *
 * Reads the same before and after repair(), and is emptied by the next verify().
 */
static PyObject* get_renames(Par2RepairerObject* self, void*) {
    if (self->verifier == NULL)
        Py_RETURN_NONE;

    PyObject* dict = PyDict_New();
    if (dict == NULL)
        return NULL;

    const std::vector<std::pair<std::string, std::string> > renamed =
        without_gil([&] { return self->verifier->GetRenamedFiles(); });

    for (size_t i = 0; i < renamed.size(); i++) {
        PyObject* key = name_to_python(renamed[i].first);
        PyObject* value = name_to_python(renamed[i].second);
        if (key == NULL || value == NULL || PyDict_SetItem(dict, key, value) < 0) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            Py_DECREF(dict);
            return NULL;
        }
        Py_DECREF(key);
        Py_DECREF(value);
    }
    return dict;
}

/*
 * One dict per file in the set, in par2's own order:
 *   name    - the name recorded in the par2 set
 *   target  - where that file belongs on this system, absolute
 *   size    - the file's size in bytes
 *   blocks  - the file's block count, 0 if it cannot be recovered
 *   hash16k - the MD5 of the file's first 16 KiB, 16 bytes
 *
 * Both names are safe to use as they stand - an absolute path or one climbing out
 * with ".." is defused before either is reported - but either may still contain a
 * directory separator, because a set may describe files in subdirectories.
 */
static PyObject* get_files(Par2RepairerObject* self, void*) {
    if (self->verifier == NULL)
        Py_RETURN_NONE;

    PyObject* list = PyList_New(0);
    if (list == NULL)
        return NULL;

    std::vector<par2::Par2FileInfo> files;
    if (!without_gil([&] { return self->verifier->GetFileInfo(&files); }))
        return list;

    for (size_t i = 0; i < files.size(); i++) {
        PyObject* entry = Py_BuildValue("{s:N, s:N, s:K, s:I, s:y#}",
                                        "name", name_to_python(files[i].filename),
                                        "target", name_to_python(files[i].localfilename),
                                        "size", (unsigned long long)files[i].filesize,
                                        "blocks", files[i].blockcount,
                                        "hash16k", (const char*)files[i].hash16k.data(), (Py_ssize_t)16);
        if (entry == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        if (PyList_Append(list, entry) < 0) {
            Py_DECREF(entry);
            Py_DECREF(list);
            return NULL;
        }
        Py_DECREF(entry);
    }
    return list;
}

/*
 * The damaged files repair() renamed out of the way, which is what par2's own purge
 * option deletes. Files the caller supplied as extra files are never listed, even
 * where their blocks were used.
 */
static PyObject* get_backup_files(Par2RepairerObject* self, void*) {
    if (self->verifier == NULL)
        Py_RETURN_NONE;

    const std::vector<std::string> backups =
        without_gil([&] { return self->verifier->GetBackupFiles(); });

    PyObject* list = PyList_New((Py_ssize_t)backups.size());
    if (list == NULL)
        return NULL;

    for (size_t i = 0; i < backups.size(); i++) {
        PyObject* name = name_to_python(backups[i]);
        if (name == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, (Py_ssize_t)i, name);
    }
    return list;
}

/*
 * Why the last call failed, or None where it did not. Describes the call that
 * returned last and the first thing that went wrong during it; error_callback sees
 * every one of them as it happens.
 */
static PyObject* get_last_error(Par2RepairerObject* self, void*) {
    if (self->verifier == NULL)
        Py_RETURN_NONE;

    par2::Par2Error error;
    if (!without_gil([&] { return self->verifier->GetLastError(&error); }))
        Py_RETURN_NONE;

    return Py_BuildValue("{s:N, s:N, s:N}",
                         "code", enum_value(Par2ErrorCodeType, (int)error.code),
                         "message", message_to_python(error.message),
                         "filename", name_to_python(error.filename));
}

static PyObject* get_callback(PyObject* callback) {
    if (callback == NULL)
        Py_RETURN_NONE;
    Py_INCREF(callback);
    return callback;
}

static int set_callback(PyObject** slot, PyObject* value, const char* name) {
    if (value == NULL || value == Py_None) {
        Py_CLEAR(*slot);
        return 0;
    }
    if (!PyCallable_Check(value)) {
        PyErr_Format(PyExc_TypeError, "%s must be callable or None", name);
        return -1;
    }
    Py_INCREF(value);
    Py_XSETREF(*slot, value);
    return 0;
}

static PyObject* get_progress_callback(Par2RepairerObject* self, void*) {
    return get_callback(self->progress_callback);
}

static int set_progress_callback(Par2RepairerObject* self, PyObject* value, void*) {
    return set_callback(&self->progress_callback, value, "progress_callback");
}

static PyObject* get_file_done_callback(Par2RepairerObject* self, void*) {
    return get_callback(self->file_done_callback);
}

static int set_file_done_callback(Par2RepairerObject* self, PyObject* value, void*) {
    return set_callback(&self->file_done_callback, value, "file_done_callback");
}

static PyObject* get_error_callback(Par2RepairerObject* self, void*) {
    return get_callback(self->error_callback);
}

static int set_error_callback(Par2RepairerObject* self, PyObject* value, void*) {
    return set_callback(&self->error_callback, value, "error_callback");
}

static PyObject* get_warning_callback(Par2RepairerObject* self, void*) {
    return get_callback(self->warning_callback);
}

static int set_warning_callback(Par2RepairerObject* self, PyObject* value, void*) {
    return set_callback(&self->warning_callback, value, "warning_callback");
}

static PyGetSetDef Par2Repairer_getset[] = {
    {"missing_block_count", (getter)get_missing_block_count, NULL,
     "Blocks that need reconstructing.", NULL},
    {"available_block_count", (getter)get_available_block_count, NULL,
     "Undamaged source blocks found.", NULL},
    {"source_block_count", (getter)get_source_block_count, NULL, "Blocks in the complete set.",
     NULL},
    {"recovery_block_count", (getter)get_recovery_block_count, NULL,
     "Recovery blocks loaded from the par2 files.", NULL},
    {"recoverable_file_count", (getter)get_recoverable_file_count, NULL,
     "Files the set can recover.", NULL},
    {"complete_file_count", (getter)get_complete_file_count, NULL, "Files verified intact.", NULL},
    {"damaged_file_count", (getter)get_damaged_file_count, NULL, "Files present but damaged.",
     NULL},
    {"missing_file_count", (getter)get_missing_file_count, NULL, "Files not found.", NULL},
    {"renamed_file_count", (getter)get_renamed_file_count, NULL,
     "Files found under a different name.", NULL},
    {"block_size", (getter)get_block_size, NULL, "Block size of the set, in bytes.", NULL},
    {"data_size", (getter)get_data_size, NULL, "Total size of the recoverable files, in bytes.",
     NULL},
    {"setid", (getter)get_setid, NULL, "The par2 set id, 16 bytes.", NULL},
    {"creator", (getter)get_creator, NULL,
     "The client that created the set, as its creator packet records it. Empty\n"
     "until load(), and where the set carries none.", NULL},
    {"repair_possible", (getter)get_repair_possible, NULL,
     "Whether enough recovery blocks are available to repair.", NULL},
    {"cancelled", (getter)get_cancelled, NULL, "Whether cancel() stopped an operation.", NULL},
    {"quick_verified_files", (getter)get_quick_verified_files, NULL,
     "How many files verify() will take from set_known_blocks() instead of reading.", NULL},
    {"in_place_files", (getter)get_in_place_files, NULL,
     "The files the last repair() repaired in place, by the names the set records.", NULL},
    {"renames", (getter)get_renames, NULL,
     "{path_on_disk: path_it_belongs_under} for files par2 matched under another name.",
     NULL},
    {"files", (getter)get_files, NULL, "Per-file state as a list of dicts.", NULL},
    {"backup_files", (getter)get_backup_files, NULL,
     "Damaged files repair() renamed out of the way.", NULL},
    {"last_error", (getter)get_last_error, NULL,
     "Why the last call failed as {code, message, filename}, or None if it did not.",
     NULL},
    {"progress_callback", (getter)get_progress_callback, (setter)set_progress_callback,
     "Callable invoked as (stage, filename, percent), or None.", NULL},
    {"file_done_callback", (getter)get_file_done_callback, (setter)set_file_done_callback,
     "Callable invoked as (filename, blocks_found, blocks_total) once per file,\n"
     "or None. blocks_found > 0 means that file contributed data to the repair;\n"
     "both counts are 0 for a par2 file.", NULL},
    {"error_callback", (getter)get_error_callback, (setter)set_error_callback,
     "Callable invoked as (code, message, filename) once per error, or None.\n"
     "code is a Par2ErrorCode. The operation may still succeed.", NULL},
    {"warning_callback", (getter)get_warning_callback, (setter)set_warning_callback,
     "Callable invoked as (code, message, filename) once per warning, or None.\n"
     "code is a Par2WarningCode. Nothing else reports these.", NULL},
    {NULL, NULL, NULL, NULL, NULL}};

static PyTypeObject Par2RepairerType = {
    PyVarObject_HEAD_INIT(NULL, 0) "sabctools.Par2Repairer",  // tp_name
    sizeof(Par2RepairerObject),                               // tp_basicsize
    0,                                                        // tp_itemsize
    (destructor)Par2Repairer_dealloc,                         // tp_dealloc
    0,                                                        // tp_vectorcall_offset
    0,                                                        // tp_getattr
    0,                                                        // tp_setattr
    0,                                                        // tp_as_async
    0,                                                        // tp_repr
    0,                                                        // tp_as_number
    0,                                                        // tp_as_sequence
    0,                                                        // tp_as_mapping
    0,                                                        // tp_hash
    0,                                                        // tp_call
    0,                                                        // tp_str
    0,                                                        // tp_getattro
    0,                                                        // tp_setattro
    0,                                                        // tp_as_buffer
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,                  // tp_flags
    "Par2Repairer(parfile, extrafiles=(), basepath='', ...)",  // tp_doc
    (traverseproc)Par2Repairer_traverse,                      // tp_traverse
    (inquiry)Par2Repairer_clear,                              // tp_clear
    0,                                                        // tp_richcompare
    0,                                                        // tp_weaklistoffset
    0,                                                        // tp_iter
    0,                                                        // tp_iternext
    Par2Repairer_methods,                                     // tp_methods
    0,                                                        // tp_members
    Par2Repairer_getset,                                      // tp_getset
    0,                                                        // tp_base
    0,                                                        // tp_dict
    0,                                                        // tp_descr_get
    0,                                                        // tp_descr_set
    0,                                                        // tp_dictoffset
    (initproc)Par2Repairer_init,                              // tp_init
    PyType_GenericAlloc,                                      // tp_alloc
    Par2Repairer_new,                                          // tp_new
};

/* ------------------------------------------------------------------------- */

/* Add an IntEnum of the given members to the module, stealing the members dict and
   leaving a reference of our own in slot. */
static int add_int_enum(PyObject* m, const char* name, PyObject* members, PyObject** slot) {
    if (members == NULL)
        return 0;

    PyObject* enum_module = PyImport_ImportModule("enum");
    if (enum_module == NULL) {
        Py_DECREF(members);
        return 0;
    }

    PyObject* type = PyObject_CallMethod(enum_module, "IntEnum", "(sO)", name, members);
    Py_DECREF(enum_module);
    Py_DECREF(members);
    if (type == NULL)
        return 0;

    Py_INCREF(type);
    if (PyModule_AddObject(m, name, type) < 0) {
        Py_DECREF(type);
        Py_DECREF(type);
        return 0;
    }
    *slot = type;
    return 1;
}

int par2_init(PyObject* m) {
    if (PyType_Ready(&Par2RepairerType) < 0)
        return 0;

    /* Mirrors libpar2.h's Result enum. */
    if (!add_int_enum(m, "Par2Result", Py_BuildValue(
        "{s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i}",
        "SUCCESS", (int)par2::eSuccess,
        "REPAIR_POSSIBLE", (int)par2::eRepairPossible,
        "REPAIR_NOT_POSSIBLE", (int)par2::eRepairNotPossible,
        "INVALID_COMMAND_LINE_ARGUMENTS", (int)par2::eInvalidCommandLineArguments,
        "INSUFFICIENT_CRITICAL_DATA", (int)par2::eInsufficientCriticalData,
        "REPAIR_FAILED", (int)par2::eRepairFailed,
        "FILE_IO_ERROR", (int)par2::eFileIOError,
        "LOGIC_ERROR", (int)par2::eLogicError,
        "MEMORY_ERROR", (int)par2::eMemoryError,
        "CANCELLED", (int)par2::eCancelled), &Par2ResultType))
        return 0;

    /* Mirrors libpar2.h's ErrorCode enum, which refines a failing Par2Result. */
    if (!add_int_enum(m, "Par2ErrorCode", Py_BuildValue(
        "{s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i}",
        "NONE", (int)par2::ecNone,
        "NOT_VERIFIED", (int)par2::ecNotVerified,
        "INVALID_SETTING", (int)par2::ecInvalidSetting,
        "PAR2_FILE_MISSING", (int)par2::ecPar2FileMissing,
        "MAIN_PACKET_MISSING", (int)par2::ecMainPacketMissing,
        "FILE_DESCRIPTION_MISSING", (int)par2::ecFileDescriptionMissing,
        "DUPLICATE_SOURCE_FILE", (int)par2::ecDuplicateSourceFile,
        "TOO_MANY_SOURCE_BLOCKS", (int)par2::ecTooManySourceBlocks,
        "FILE_OPEN_FAILED", (int)par2::ecFileOpenFailed,
        "FILE_CREATE_FAILED", (int)par2::ecFileCreateFailed,
        "FILE_RENAME_FAILED", (int)par2::ecFileRenameFailed,
        "FILE_READ_FAILED", (int)par2::ecFileReadFailed,
        "FILE_WRITE_FAILED", (int)par2::ecFileWriteFailed,
        "OUT_OF_MEMORY", (int)par2::ecOutOfMemory,
        "PROCESSOR_FAILED", (int)par2::ecProcessorFailed,
        "INTERNAL_ERROR", (int)par2::ecInternalError,
        "RECOVERY_UNSOLVABLE", (int)par2::ecRecoveryUnsolvable), &Par2ErrorCodeType))
        return 0;

    /* Mirrors libpar2.h's WarningCode enum. */
    if (!add_int_enum(m, "Par2WarningCode", Py_BuildValue(
        "{s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i}",
        "NONE", (int)par2::wcNone,
        "FILENAME_UNSAFE", (int)par2::wcFilenameUnsafe,
        "FILENAME_CHANGED", (int)par2::wcFilenameChanged,
        "INCOMPLETE_WRITE", (int)par2::wcIncompleteWrite,
        "INCOMPLETE_READ", (int)par2::wcIncompleteRead,
        "PACKET_DISCARDED", (int)par2::wcPacketDiscarded,
        "RENAME_FAILED", (int)par2::wcRenameFailed,
        "NOT_REPAIRED_IN_PLACE", (int)par2::wcNotRepairedInPlace,
        "IN_PLACE_INCOMPLETE", (int)par2::wcInPlaceIncomplete), &Par2WarningCodeType))
        return 0;

    Par2Error = PyErr_NewException("sabctools.Par2Error", NULL, NULL);
    if (Par2Error == NULL)
        return 0;
    Py_INCREF(Par2Error);
    if (PyModule_AddObject(m, "Par2Error", Par2Error) < 0) {
        Py_DECREF(Par2Error);
        return 0;
    }

    if (PyModule_AddType(m, &Par2RepairerType) < 0)
        return 0;

    return 1;
}
