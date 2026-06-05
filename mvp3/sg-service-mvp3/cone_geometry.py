"""Pure Python math helpers for sensor cone CZML packets.

Ported from MVP1's ``stk_com_service.py`` — same algorithm, extracted into a
standalone module so it can be tested without STK and reused by the mock
service.
"""
from __future__ import annotations
import math

_WGS84_A  = 6_378_137.0
_WGS84_E2 = 0.006_694_379_990_14


def ecef_to_lonlat_rad(x: float, y: float, z: float) -> tuple[float, float]:
    """Return ``(lon_rad, lat_rad)`` for an ECEF point (altitude not needed)."""
    lon = math.atan2(y, x)
    p   = math.sqrt(x * x + y * y)
    lat = math.atan2(z, p)
    for _ in range(10):
        n   = _WGS84_A / math.sqrt(1.0 - _WGS84_E2 * math.sin(lat) ** 2)
        lat = math.atan2(z + _WGS84_E2 * n * math.sin(lat), p)
    return lon, lat


def enu_quaternion(lon: float, lat: float) -> tuple[float, float, float, float]:
    """Quaternion ``(x, y, z, w)`` for the ENU frame at (lon_rad, lat_rad).

    Local axes in ECEF: X=East, Y=North, Z=Up (zenith).
    """
    sl, cl = math.sin(lon), math.cos(lon)
    sp, cp = math.sin(lat), math.cos(lat)
    m00, m01, m02 = -sl,     -sp * cl,  cp * cl
    m10, m11, m12 =  cl,     -sp * sl,  cp * sl
    m20, m21, m22 =  0.0,         cp,     sp
    tr = m00 + m11 + m22
    if tr > 0:
        s = 0.5 / math.sqrt(tr + 1.0)
        return (m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25 / s
    if m00 > m11 and m00 > m22:
        s = 2.0 * math.sqrt(1.0 + m00 - m11 - m22)
        return 0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s
    if m11 > m22:
        s = 2.0 * math.sqrt(1.0 + m11 - m00 - m22)
        return (m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s
    s = 2.0 * math.sqrt(1.0 + m22 - m00 - m11)
    return (m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s


def _quat_align_z_to(dx: float, dy: float, dz: float) -> tuple[float, float, float, float]:
    """Shortest-arc quaternion ``(x, y, z, w)`` rotating local +Z onto a unit
    direction ``(dx, dy, dz)``. Degenerate cases (parallel / antiparallel)
    return identity / 180° around X respectively."""
    if dz > 0.9999:
        return (0.0, 0.0, 0.0, 1.0)
    if dz < -0.9999:
        return (1.0, 0.0, 0.0, 0.0)
    # axis = (+Z) × d = (-dy, dx, 0)
    ax, ay = -dy, dx
    a_len  = math.sqrt(ax * ax + ay * ay)
    ax, ay = ax / a_len, ay / a_len
    angle  = math.acos(max(-1.0, min(1.0, dz)))
    half   = angle * 0.5
    s      = math.sin(half)
    return (ax * s, ay * s, 0.0, math.cos(half))


def _unpack_tx(positions: list[float]) -> tuple[list[float], list[tuple[float, float, float]]]:
    """Split ``[t, x, y, z, t, x, y, z, ...]`` into parallel ``times`` and
    ``xyz`` lists."""
    times = positions[0::4]
    xyzs  = [(positions[i + 1], positions[i + 2], positions[i + 3])
             for i in range(0, len(positions), 4)]
    return times, xyzs


def nadir_quaternion_samples(positions: list[float]) -> list[float]:
    """Return time-dynamic ``[t, qx, qy, qz, qw, ...]`` quaternions that
    rotate a sensor's local +Z onto the nadir direction at each sample —
    i.e. pointing from the platform's ECEF position toward earth's centre."""
    times, xyzs = _unpack_tx(positions)
    out: list[float] = []
    for t, (x, y, z) in zip(times, xyzs):
        mag = math.sqrt(x * x + y * y + z * z)
        if mag == 0:
            out.extend([t, 0.0, 0.0, 0.0, 1.0])
            continue
        qx, qy, qz, qw = _quat_align_z_to(-x / mag, -y / mag, -z / mag)
        out.extend([t, qx, qy, qz, qw])
    return out


def aim_quaternion_samples(
    from_positions: list[float],
    to_positions:   list[float],
) -> list[float]:
    """Return time-dynamic unit quaternions that rotate a sensor's local +Z
    toward the ``to`` platform's position at each sample index. Both position
    arrays must share the same cadence; iteration stops at the shorter one."""
    f_times, f_xyzs = _unpack_tx(from_positions)
    _t_times, t_xyzs = _unpack_tx(to_positions)
    out: list[float] = []
    n = min(len(f_xyzs), len(t_xyzs))
    for i in range(n):
        t = f_times[i]
        fx, fy, fz = f_xyzs[i]
        tx, ty, tz = t_xyzs[i]
        dx, dy, dz = tx - fx, ty - fy, tz - fz
        mag = math.sqrt(dx * dx + dy * dy + dz * dz)
        if mag == 0:
            out.extend([t, 0.0, 0.0, 0.0, 1.0])
            continue
        qx, qy, qz, qw = _quat_align_z_to(dx / mag, dy / mag, dz / mag)
        out.extend([t, qx, qy, qz, qw])
    return out


def cone_samples(
    parent_cartesian: list[float],
    cylinder_length: float,
) -> tuple[list[float], list[float]]:
    """Derive (cartesian, unitQuaternion) CZML sample arrays for a sensor cone.

    Input: parent aircraft ECEF samples as ``[t, x, y, z, t, x, y, z, ...]``.
    Output:
      * ``cartesian``  — shifted centres, same ``[t, x, y, z, ...]`` layout.
        Each centre is at ``parent − (length / 2) · zenith_unit`` so the
        cylinder's local +Z apex (topRadius=0) sits exactly at the parent.
      * ``unitQuaternion`` — ENU-frame quaternions, ``[t, qx, qy, qz, qw, ...]``.
    """
    half_shift = cylinder_length / 2.0
    cart, quat = [], []
    for i in range(0, len(parent_cartesian), 4):
        t, x, y, z = (parent_cartesian[i], parent_cartesian[i + 1],
                      parent_cartesian[i + 2], parent_cartesian[i + 3])
        lon, lat = ecef_to_lonlat_rad(x, y, z)
        zx = math.cos(lat) * math.cos(lon)
        zy = math.cos(lat) * math.sin(lon)
        zz = math.sin(lat)
        cart.extend([t, x - half_shift * zx, y - half_shift * zy, z - half_shift * zz])
        qx, qy, qz, qw = enu_quaternion(lon, lat)
        quat.extend([t, qx, qy, qz, qw])
    return cart, quat
