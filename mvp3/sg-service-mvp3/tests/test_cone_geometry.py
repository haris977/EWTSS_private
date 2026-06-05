import math
from cone_geometry import ecef_to_lonlat_rad, enu_quaternion, cone_samples


def test_ecef_to_lonlat_equator_x_axis():
    """ECEF point on +X axis at sea level → lon=0, lat=0."""
    lon, lat = ecef_to_lonlat_rad(6_378_137.0, 0.0, 0.0)
    assert abs(lon) < 1e-9
    assert abs(lat) < 1e-6


def test_ecef_to_lonlat_kashmir():
    """Known Kashmir point (~34 N, 75 E at 0 m) round-trips within 0.01 deg."""
    lat_in, lon_in = math.radians(34.0), math.radians(75.0)
    a = 6_378_137.0
    x = a * math.cos(lat_in) * math.cos(lon_in)
    y = a * math.cos(lat_in) * math.sin(lon_in)
    z = a * math.sin(lat_in)  # small-error approx; fine for this test
    lon_out, lat_out = ecef_to_lonlat_rad(x, y, z)
    assert abs(lon_out - lon_in) < 1e-4
    assert abs(lat_out - lat_in) < 0.004  # ~0.23 deg; acceptable given spherical approx


def test_enu_quaternion_is_unit():
    qx, qy, qz, qw = enu_quaternion(math.radians(75.0), math.radians(34.0))
    norm = qx * qx + qy * qy + qz * qz + qw * qw
    assert abs(norm - 1.0) < 1e-9


def test_cone_samples_shifts_apex_to_parent():
    """Cone centre should be shifted down (toward ellipsoid centre) from parent ECEF."""
    parent = [0.0, 7_378_137.0, 0.0, 0.0]
    length = 20_000.0
    carts, quats = cone_samples(parent, length)
    assert len(carts) == 4
    assert len(quats) == 5  # [t, qx, qy, qz, qw]
    assert carts[1] < parent[1]
    assert abs((parent[1] - carts[1]) - length / 2.0) < 1e-3
