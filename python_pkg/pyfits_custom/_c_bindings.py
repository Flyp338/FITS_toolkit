import ctypes
from ctypes import (
    POINTER,
    Structure,
    c_char,
    c_char_p,
    c_double,
    c_int,
    c_int64,
    c_size_t,
    c_uint8,
    c_void_p,
)
import os
import platform
import sys

# -----------------------------------------------------------------------------
# Library Loading
# -----------------------------------------------------------------------------


def _load_libfits():
    system = platform.system()
    if system == "Windows":
        lib_name = "libfits.dll"
    elif system == "Darwin":
        lib_name = "libfits.dylib"
    else:
        lib_name = "libfits.so"

    # Search paths: build directory relative to package, cwd, or environment variable
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(pkg_dir, "..", ".."))

    search_paths = [
        os.path.join(repo_root, "build", lib_name),
        os.path.join(repo_root, "build", "Release", lib_name),
        os.path.join(repo_root, "build", "Debug", lib_name),
        os.path.join(".", lib_name),
    ]

    if "LIBFITS_PATH" in os.environ:
        search_paths.insert(0, os.environ["LIBFITS_PATH"])

    for path in search_paths:
        if os.path.exists(path):
            try:
                return ctypes.CDLL(path)
            except OSError as e:
                raise ImportError(f"Found libfits at {path} but failed to load: {e}")

    raise ImportError(
        f"Could not locate {lib_name}. Ensure libfits is compiled in build/ "
        f"or set LIBFITS_PATH."
    )


libfits = _load_libfits()

# -----------------------------------------------------------------------------
# C Struct Mappings
# -----------------------------------------------------------------------------


class FitsCardC(Structure):
    _fields_ = [
        ("key", c_char * 9),  # 8 chars + null terminator
        ("value", c_char * 71),  # Value string
        ("comment", c_char * 73),  # Comment string
    ]


class FitsHduC(Structure):
    _fields_ = [
        ("hdu_index", c_int),
        ("bitpix", c_int),
        ("naxis", c_int),
        ("naxis_dims", c_int64 * 99),
        ("card_count", c_size_t),
        ("cards", POINTER(FitsCardC)),
        ("data_offset", c_size_t),
        ("data_size_bytes", c_size_t),
        ("raw_data_ptr", c_void_p),
    ]


class FitsFileC(Structure):
    _fields_ = [
        ("filepath", c_char_p),
        ("file_size", c_size_t),
        ("mmap_ptr", c_void_p),
        ("hdu_count", c_size_t),
        ("hdus", POINTER(FitsHduC)),
    ]


# -----------------------------------------------------------------------------
# Function Signature Binds
# -----------------------------------------------------------------------------

# FitsFile* fits_open(const char* filepath);
libfits.fits_open.argtypes = [c_char_p]
libfits.fits_open.restype = POINTER(FitsFileC)

# void fits_close(FitsFile* fits);
libfits.fits_close.argtypes = [POINTER(FitsFileC)]
libfits.fits_close.restype = None

# int fits_get_hdu_count(const FitsFile* fits);
libfits.fits_get_hdu_count.argtypes = [POINTER(FitsFileC)]
libfits.fits_get_hdu_count.restype = c_int

# FitsHdu* fits_get_hdu(const FitsFile* fits, int index);
libfits.fits_get_hdu.argtypes = [POINTER(FitsFileC), c_int]
libfits.fits_get_hdu.restype = POINTER(FitsHduC)

# int fits_read_data_byteswap(const FitsHdu* hdu, void* dest_buffer, size_t dest_size);
if hasattr(libfits, "fits_read_data_byteswap"):
    libfits.fits_read_data_byteswap.argtypes = [POINTER(FitsHduC), c_void_p, c_size_t]
    libfits.fits_read_data_byteswap.restype = c_int
