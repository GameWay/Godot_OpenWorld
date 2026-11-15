"""Functions used to generate source files during build time"""

import hashlib
import os

import methods


def _script_files(scripts_dir):
    files = []
    for root, dirs, names in os.walk(scripts_dir):
        dirs.sort()
        for name in sorted(names):
            path = os.path.join(root, name)
            files.append((os.path.relpath(path, scripts_dir).replace("\\", "/"), path))
    return files


def script_sources(scripts_dir):
    return [path for _rel, path in _script_files(scripts_dir)]


def make_scripts_header(target, source, env):
    scripts_dir = os.path.dirname(str(source[0]))
    # Archive: for every file, a little-endian u32 path length, the UTF-8 path, a u32 size and the data.
    buffer = bytearray()
    for rel, path in _script_files(scripts_dir):
        encoded = rel.encode("utf-8")
        data = methods.get_buffer(path)
        buffer += len(encoded).to_bytes(4, "little") + encoded
        buffer += len(data).to_bytes(4, "little") + data

    digest = hashlib.sha256(buffer).hexdigest()[:16]
    uncompressed_size = len(buffer)
    compressed = methods.compress_buffer_zstd(bytes(buffer), env)

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(f"""\
inline constexpr const char *_vc_git_scripts_hash = "{digest}";
inline constexpr int _vc_git_scripts_compressed_size = {len(compressed)};
inline constexpr int _vc_git_scripts_uncompressed_size = {uncompressed_size};
inline constexpr unsigned char _vc_git_scripts_compressed[] = {{
	{methods.format_buffer(compressed, 1)}
}};
""")
