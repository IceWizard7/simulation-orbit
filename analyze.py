from __future__ import annotations
import sys
import dataclasses
from typing import Any

import requests
import re
import datetime
import math
import pathlib
import shlex
import typing
import csv
import itertools

planet_to_horizon_id: dict[str, int] = {
    # Sun
    "Sun": 10,

    # Planets
    "Mercury": 199,
    "Venus": 299,
    "Earth": 399,
    "Mars": 499,
    "Jupiter": 599,
    "Saturn": 699,
    "Uranus": 799,
    "Neptune": 899,

    # Dwarf Planets
    "Pluto": 999,

    # Moons of Earth
    "Moon": 301,

    # Moons of Mars
    "Phobos": 401,
    "Deimos": 402,

    # Moons of Jupiter
    "Io": 501,
    "Europa": 502,
    "Ganymede": 503,
    "Callisto": 504,

    # Moons of Saturn
    "Mimas": 601,
    "Enceladus": 602,
    "Tethys": 603,
    "Dione": 604,
    "Rhea": 605,
    "Titan": 606,
    "Hyperion": 607,
    "Iapetus": 608,
    "Phoebe": 609,

    # Moons of Uranus
    "Ariel": 701,
    "Umbriel": 702,
    "Titania": 703,
    "Oberon": 704,
    "Miranda": 705,

    # Moons of Neptune
    "Triton": 801,
    "Nereid": 802,
    "Proteus": 808,

    # Moons of Pluto
    "Charon": 901,
    "Nix": 902,
    "Hydra": 903,
    "Kerberos": 904
}

moon_to_parent: dict[str, str] = {
    "Moon": "Earth",
    "Phobos": "Mars",
    "Deimos": "Mars",
    "Io": "Jupiter",
    "Europa": "Jupiter",
    "Ganymede": "Jupiter",
    "Callisto": "Jupiter",
    "Mimas": "Saturn",
    "Enceladus": "Saturn",
    "Tethys": "Saturn",
    "Dione": "Saturn",
    "Rhea": "Saturn",
    "Titan": "Saturn",
    "Hyperion": "Saturn",
    "Iapetus": "Saturn",
    "Phoebe": "Saturn",
    "Ariel": "Uranus",
    "Umbriel": "Uranus",
    "Titania": "Uranus",
    "Oberon": "Uranus",
    "Miranda": "Uranus",
    "Triton": "Neptune",
    "Nereid": "Neptune",
    "Proteus": "Neptune",
    "Charon": "Pluto",
    "Nix": "Pluto",
    "Hydra": "Pluto",
    "Kerberos": "Pluto"
}

# The barycentric approximation deliberately uses exactly the bodies modeled by
# the C++ simulator, rather than Horizons' broader official planet-system membership.
planetary_system_to_moons: dict[str, tuple[str, ...]] = {
    "Earth": ("Moon",),
    "Mars": ("Phobos", "Deimos"),
    "Jupiter": ("Io", "Europa", "Ganymede", "Callisto"),
    "Saturn": ("Mimas", "Enceladus", "Tethys", "Dione", "Rhea", "Titan", "Hyperion", "Iapetus", "Phoebe"),
    "Uranus": ("Ariel", "Umbriel", "Titania", "Oberon", "Miranda"),
    "Neptune": ("Triton", "Nereid", "Proteus"),
    "Pluto": ("Charon", "Nix", "Hydra", "Kerberos"),
}

# GM in m^3/s^2
# Phobos/Deimos: JPL satellite solution values (Horizons lists only Mass for 401/402).
# Nereid: no mass or GM anywhere in Horizons
# => this is a density-based estimate (~3.1e19 kg), fine since Nereid is tiny
hardcoded_gm: dict[int, float] = {
    401: 7.087546066894452e-4 * 1e9,  # Phobos ≈ 7.0875e5 m^3/s^2
    402: 9.615569648120313e-5 * 1e9,  # Deimos ≈ 9.6156e4 m^3/s^2
    802: 2.07e9,                      # Nereid ≈ 3.1e19 kg * G
}

# Exact original GM values used by src/simulation.cpp. Offline CSV-to-CSV
# barycentre analysis needs these weights without making a Horizons request.
body_gravitational_mass: dict[str, float] = {
    "Sun": 1.3271244004193937e20,
    "Mercury": 22031868550000.0,
    "Venus": 324858592000000.0,
    "Earth": 398600435436000.0,
    "Mars": 42828375662000.0,
    "Jupiter": 1.266865319e17,
    "Saturn": 3.7931206234e16,
    "Uranus": 5793950610300000.0,
    "Neptune": 6835099970000000.0,
    "Pluto": 869326000000.0,
    "Moon": 4902800066000.0,
    "Phobos": 708754.6066894452,
    "Deimos": 96155.69648120314,
    "Io": 5959915500000.0,
    "Europa": 3202712100000.0,
    "Ganymede": 9887832800000.0,
    "Callisto": 7179283400000.0,
    "Mimas": 2503489000.0,
    "Enceladus": 7210367000.0,
    "Tethys": 41210000000.0,
    "Dione": 73116000000.0,
    "Rhea": 153940000000.0,
    "Titan": 8978140000000.0,
    "Hyperion": 370500000.0,
    "Iapetus": 120520000000.0,
    "Phoebe": 554800000.0,
    "Ariel": 83430000000.0,
    "Umbriel": 85400000000.0,
    "Titania": 222800000000.0,
    "Oberon": 205340000000.0,
    "Miranda": 4300000000.0,
    "Triton": 1428495000000.0,
    "Nereid": 2070000000.0,
    "Proteus": 2580000000.0,
    "Charon": 106100000000.0,
    "Nix": 1496000.0,
    "Hydra": 2010000.0,
    "Kerberos": 60380.0,
}

OBLIQUITY_J2000_DEG: float = 23.4392911  # IAU 2006 mean obliquity of the ecliptic at Year 2000

@dataclasses.dataclass(frozen=True)
class Oblateness:
    j2: float
    equatorial_radius_m: float  # https://ssd.jpl.nasa.gov/planets/phys_par.html
    pole_ra_deg: float
    pole_dec_deg: float

# At the time of writing this, https://www.nasa.gov/nssdc/ returns: "The NASA Space Science Data Coordinated Archive website is temporarily offline for maintenance."
# That's why the sources here are wayback snapshots
# equatorial_radius_m: https://ssd.jpl.nasa.gov/planets/phys_par.html NOT from the other NASA links.
planet_oblateness: dict[str, Oblateness] = {
    # j2, R_eq (m), pole RA, pole Dec
    "Earth":   Oblateness(1082.63e-6, 6378.1366e3, 0.0, 90.0),  # http://web.archive.org/web/20250821225047/https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html
    "Mars":    Oblateness(1960.45e-6, 3396.19e3, 317.681, 52.887),  # https://web.archive.org/web/20250820142225/https://nssdc.gsfc.nasa.gov/planetary/factsheet/marsfact.html
    "Jupiter": Oblateness(14736e-6, 71492e3, 268.057, 64.495),  # https://web.archive.org/web/20250813051413/https://nssdc.gsfc.nasa.gov/planetary/factsheet/jupiterfact.html
    "Saturn":  Oblateness(16298e-6, 60268e3, 40.589, 83.537),  # https://web.archive.org/web/20250821165423/https://nssdc.gsfc.nasa.gov/planetary/factsheet/saturnfact.html
    "Uranus":  Oblateness(3343.43e-6, 25559e3, 257.311, -15.175),  # https://web.archive.org/web/20250723171354/https://nssdc.gsfc.nasa.gov/planetary/factsheet/uranusfact.html
    "Neptune": Oblateness(3411e-6, 24764e3, 299.36, 43.46),  # https://web.archive.org/web/20250723171356/https://nssdc.gsfc.nasa.gov/planetary/factsheet/neptunefact.html
}

AU: float = 149_597_870_700  # astronomical unit (meters)
EPS: float = 1e-12  # epsilon
START = datetime.datetime(1800, 1, 3)
SECONDS_PER_YEAR = 86_400 * 365  # assuming year @ 365 days

def scientific_notation_to_float(scientific_notation: str) -> float:
    mantissa: str = scientific_notation.split("e")[0]
    exponent: str = scientific_notation.split("e")[1]
    return float(mantissa) * 10 ** int(exponent)

@dataclasses.dataclass(frozen=True)
class Vec3:
    x: float
    y: float
    z: float

    def __add__(self, other: object) -> Vec3:
        if isinstance(other, Vec3):
            return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)
        return NotImplemented

    def __sub__(self, other: object) -> Vec3:
        if isinstance(other, Vec3):
            return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)
        return NotImplemented

    def __truediv__(self, other: object) -> Vec3:
        if isinstance(other, (float, int)):
            return Vec3(self.x / other, self.y / other, self.z / other)
        return NotImplemented

    def __mul__(self, other: object) -> Vec3:
        if isinstance(other, (float, int)):
            return Vec3(self.x * other, self.y * other, self.z * other)
        return NotImplemented

    __rmul__ = __mul__

    def length(self) -> float:
        return math.sqrt(self.x ** 2 + self.y ** 2 + self.z ** 2)

    def dot(self, other: Vec3) -> float:
        return (self.x * other.x) + (self.y * other.y) + (self.z * other.z)

    def cross(self, other: Vec3) -> Vec3:
        return Vec3(
            self.y * other.z - self.z * other.y,
            self.z * other.x - self.x * other.z,
            self.x * other.y - self.y * other.x,
        )

    def angle_degrees(self, other: Vec3) -> float:
        denominator = self.length() * other.length()

        if denominator < EPS:
            return float("nan")  # if one (or both) vectors don't have a length, angle is undefined

        c: float = self.dot(other) / denominator
        c = max(-1, min(1, c))
        return math.degrees(math.acos(c))

    def __repr__(self) -> str:
        return str(self)

    def __str__(self) -> str:
        return f"[{self.x}, {self.y}, {self.z}]"

@dataclasses.dataclass(frozen=True)
class ProgramRun:
    computation_seconds: float
    years: float
    steps: int
    dt: float
    vectors: list[Vec3]
    enabled_body_indices: frozenset[int]
    body_set: str
    invocation_command: str

@dataclasses.dataclass(frozen=True)
class ErrorMetrics:
    distance_m: float
    relative_percent: float
    angular_degrees: float
    radial_m: float

@dataclasses.dataclass(frozen=True)
class BodyState:
    position: Vec3
    velocity: Vec3

@dataclasses.dataclass(frozen=True)
class CSVSample:
    step: int
    simulated_seconds: int
    bodies: dict[str, BodyState]

def planetary_system_members(parent: str) -> tuple[str, ...]:
    return parent, *planetary_system_to_moons.get(parent, ())

def weighted_system_state(sample: CSVSample, parent: str, require_explicit_moons: bool) -> BodyState:
    if parent not in sample.bodies:
        raise RuntimeError(
            f"CSV sample has no {parent} state at simulated_seconds={sample.simulated_seconds}"
        )

    moons = planetary_system_to_moons.get(parent, ())
    present_moons = tuple(moon for moon in moons if moon in sample.bodies)
    if not present_moons and not require_explicit_moons:
        # In a planet-systems or naive moon-free run, the parent column is the
        # only available representation of the planetary system.
        return sample.bodies[parent]

    if present_moons != moons:
        missing = [moon for moon in moons if moon not in sample.bodies]
        raise RuntimeError(
            f"Cannot construct the {parent} system barycentre at "
            f"simulated_seconds={sample.simulated_seconds}; missing: {', '.join(missing)}"
        )

    members = planetary_system_members(parent)
    total_gm = sum(body_gravitational_mass[name] for name in members)
    weighted_position = Vec3(0.0, 0.0, 0.0)
    weighted_velocity = Vec3(0.0, 0.0, 0.0)
    for name in members:
        gm = body_gravitational_mass[name]
        weighted_position += sample.bodies[name].position * gm
        weighted_velocity += sample.bodies[name].velocity * gm

    return BodyState(weighted_position / total_gm, weighted_velocity / total_gm)

def collapse_system_positions(vectors: list[Vec3], names: list[str]) -> list[Vec3]:
    if len(vectors) != len(names):
        raise ValueError("Vectors and names must have equal lengths for barycentre construction")

    indices: dict[str, int] = {name: index for index, name in enumerate(names)}
    collapsed: list[Vec3] = list(vectors)
    for parent in planetary_system_to_moons:
        members: tuple[str, ...] = planetary_system_members(parent)
        total_gm: float = sum(body_gravitational_mass[name] for name in members)
        weighted_position: Vec3 = Vec3(0.0, 0.0, 0.0)
        for name in members:
            weighted_position += vectors[indices[name]] * body_gravitational_mass[name]
        collapsed[indices[parent]] = weighted_position / total_gm

    return collapsed

def system_output_indices(enabled_body_indices: frozenset[int], names: list[str]) -> frozenset[int]:
    parent_names: set[str] = {"Sun", "Mercury", "Venus", *planetary_system_to_moons.keys()}
    return frozenset(
        index
        for index in enabled_body_indices
        if names[index] in parent_names
    )

def require_complete_explicit_systems(
    enabled_body_indices: frozenset[int],
    names: list[str],
    source: str,
) -> None:
    enabled_names = {names[index] for index in enabled_body_indices}
    required_names = {
        name
        for parent in planetary_system_to_moons
        for name in planetary_system_members(parent)
    }
    missing_names = [name for name in names if name in required_names and name not in enabled_names]
    if missing_names:
        raise RuntimeError(
            f"{source} cannot construct every modeled planetary-system barycentre; "
            f"disabled or missing: {', '.join(missing_names)}"
        )

def read_csv_samples(path: pathlib.Path) -> list[CSVSample]:
    samples: list[CSVSample] = []
    bodies: list[str] = []

    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        field_names: list[str] = list(reader.fieldnames)  # type: ignore[unused-ignore]

        for planet in planet_to_horizon_id:
            expected_headers = [
                f'{planet}_position_x',
                f'{planet}_position_y',
                f'{planet}_position_z',
                f'{planet}_velocity_x',
                f'{planet}_velocity_y',
                f'{planet}_velocity_z'
            ]
            add_planet: bool = True
            for expected_header in expected_headers:
                if expected_header not in field_names:
                    add_planet = False
                    break
            if add_planet: bodies.append(planet)

        for row in reader:
            sample: CSVSample = CSVSample(
                int(row["step"]),
                int(row["simulated_seconds"]),
                {
                    body: BodyState(
                        Vec3(
                            float(row[f"{body}_position_x"]),
                            float(row[f"{body}_position_y"]),
                            float(row[f"{body}_position_z"])
                        ),
                        Vec3(
                            float(row[f"{body}_velocity_x"]),
                            float(row[f"{body}_velocity_y"]),
                            float(row[f"{body}_velocity_z"])
                        )
                    )
                    for body in bodies
                }
            )
            samples.append(sample)

    return samples

def parse_program_output(text: str) -> ProgramRun:
    computation_seconds_match = re.search(r"Computation time:\s*([0-9.]+)", text)
    simulated_years_match = re.search(r"Simulation time:\s*([0-9.]+)", text)
    steps_match = re.search(r"Steps simulated:\s*(\d+)", text)
    dt_match = re.search(r"Time step:\s*([0-9.eE+-]+)", text)
    invocation_match = re.search(r"Invocation command:\s*(.+)", text)
    body_set_match = re.search(r"^Body set:\s*(\S+)\s*$", text, re.MULTILINE)
    enabled_body_indices_match = re.search(r"^Enabled body indices:\s*(.*)$", text, re.MULTILINE)

    if not computation_seconds_match:
        raise RuntimeError(f"No computation_seconds_match found in {text}")

    if not simulated_years_match:
        raise RuntimeError(f"No simulated_years_match found in {text}")

    if not steps_match:
        raise RuntimeError(f"No steps_match found in {text}")

    if not dt_match:
        raise RuntimeError(f"No dt_match found in {text}")

    if not invocation_match:
        raise RuntimeError(f"No invocation_match found in {text}")

    computation_seconds: float = float(computation_seconds_match.group(1))
    simulated_years: float = float(simulated_years_match.group(1))
    steps: int = int(steps_match.group(1))
    dt: float = float(dt_match.group(1))
    invocation_command: str = invocation_match.group(1).strip()
    if body_set_match:
        body_set = body_set_match.group(1)
    else:
        # Backward compatibility for result files written before Body set metadata
        invocation_arguments = shlex.split(invocation_command)
        body_set = "all"
        for index, argument in enumerate(invocation_arguments[:-1]):
            if argument == "--body-set":
                body_set = invocation_arguments[index + 1]

    valid_body_sets = {"all", "planets", "dwarf", "planet-systems"}
    if body_set not in valid_body_sets:
        raise RuntimeError(f"Unknown body set in program output: {body_set}")

    positions_line = next(line for line in text.splitlines() if "|" in line and line.startswith("["))
    vectors = []
    for raw in re.findall(r"\[([^]]+)]", positions_line):
        x, y, z = (float(part.strip()) for part in raw.split(","))
        vectors.append(Vec3(x, y, z))

    expected_body_count = len(planet_to_horizon_id)
    if len(vectors) != expected_body_count:
        raise RuntimeError(f"Expected {expected_body_count} final position vectors, found {len(vectors)}")

    if enabled_body_indices_match:
        raw_indices = enabled_body_indices_match.group(1).strip()
        try:
            enabled_body_indices = frozenset(
                int(raw_index.strip())
                for raw_index in raw_indices.split(",")
                if raw_index.strip()
            )
        except ValueError as error:
            raise RuntimeError(f"Invalid enabled body indices: {raw_indices}") from error

        invalid_indices = sorted(
            index for index in enabled_body_indices
            if index < 0 or index >= expected_body_count
        )
        if invalid_indices:
            raise RuntimeError(f"Enabled body indices out of range: {invalid_indices}")
    else:
        raise RuntimeError(f"No enabled_body_indices_match found in {text}")

    return ProgramRun(
        computation_seconds,
        simulated_years,
        steps,
        dt,
        vectors,
        enabled_body_indices,
        body_set,
        invocation_command,
    )

def read_program_output(path_index: int) -> str:
    if len(sys.argv) > path_index:
        return pathlib.Path(sys.argv[path_index]).read_text()

    if not sys.stdin.isatty():
        text = sys.stdin.read()
        if text.strip():
            return text

    raise SystemExit(
        "Usage:\n"
        f"  python3 {sys.argv[0]} analyze program-output.txt\n"
        "or:\n"
        f"  ./cmake-build-release/simulation-orbit | python3 {sys.argv[0]} analyze"
    )

def analysis_invocation_command() -> str:
    return "python3 " + shlex.join(sys.argv)

def fetch_vectors(start_date: datetime.date) -> tuple[list[str], list[Vec3]]:
    res: list[str] = []
    positions: list[Vec3] = []

    # radius_2d, radius_3d, color, orbital_period_seconds (as "<days> * 86'400")[, texture]
    # period: rendering-only; parent-relative for moons, heliocentric for planets
    # Sun uses Pluto's period so its history (which bounds how far back center-relative
    # trails can reach) spans every other body's full tail
    fix_code: dict[str, str] = {
        "Sun": "10, 0.03, (Color){255, 230,  40, 255}, 90560.0 * 86'400, \"resources/sun.jpg\"",
        "Mercury": "5, 0.01, (Color){150, 150, 150, 255}, 87.969 * 86'400, \"resources/mercury.jpg\"",
        "Venus": "5, 0.01, (Color){245, 190,  70, 255}, 224.701 * 86'400, \"resources/venus.jpg\"",
        "Earth": "5, 0.01, (Color){ 40, 120, 204, 255}, 365.256 * 86'400, \"resources/earth.jpg\"",
        "Mars": "10, 0.02, (Color){220,  60,  40, 255}, 686.980 * 86'400, \"resources/mars.jpg\"",
        "Jupiter": "10, 0.15, (Color){220, 150,  85, 255}, 4332.589 * 86'400, \"resources/jupiter.jpg\"",
        "Saturn": "10, 0.15, (Color){235, 205, 120, 255}, 10759.22 * 86'400, \"resources/saturn.jpg\"",
        "Uranus": "10, 0.15, (Color){ 80, 220, 220, 255}, 30685.4 * 86'400, \"resources/uranus.jpg\"",
        "Neptune": "10, 0.15, (Color){ 40,  80, 230, 255}, 60189.0 * 86'400, \"resources/neptune.jpg\"",
        "Pluto": "10, 0.15, (Color){185, 155, 130, 255}, 90560.0 * 86'400",
        "Moon": "5, 0.01, (Color){200, 200, 200, 255}, 27.322 * 86'400, \"resources/moon.jpg\"",
        "Phobos": "5, 0.01, (Color){105, 93, 82, 255}, 0.3189 * 86'400",
        "Deimos": "5, 0.01, (Color){139, 118, 99, 255}, 1.2624 * 86'400",
        "Io": "5, 0.01, (Color){240, 197, 79, 255}, 1.7691 * 86'400, \"resources/io.jpg\"",
        "Europa": "5, 0.01, (Color){210, 199, 174, 255}, 3.5512 * 86'400, \"resources/europa.jpg\"",
        "Ganymede": "5, 0.01, (Color){139, 126, 112, 255}, 7.1546 * 86'400, \"resources/ganymede.jpg\"",
        "Callisto": "5, 0.01, (Color){76, 66, 60, 255}, 16.689 * 86'400, \"resources/callisto.jpg\"",
        "Mimas": "5, 0.01, (Color){207, 208, 197, 255}, 0.942 * 86'400",
        "Enceladus": "5, 0.01, (Color){231, 240, 242, 255}, 1.3702 * 86'400",
        "Tethys": "5, 0.01, (Color){202, 203, 191, 255}, 1.8878 * 86'400",
        "Dione": "5, 0.01, (Color){190, 190, 180, 255}, 2.7369 * 86'400",
        "Rhea": "5, 0.01, (Color){180, 180, 170, 255}, 4.5175 * 86'400",
        "Titan": "5, 0.01, (Color){201, 141, 71, 255}, 15.9454 * 86'400",
        "Hyperion": "5, 0.01, (Color){136, 99, 70, 255}, 21.2766 * 86'400",
        "Iapetus": "5, 0.01, (Color){154, 137, 108, 255}, 79.3215 * 86'400",
        "Phoebe": "5, 0.01, (Color){71, 68, 67, 255}, 550.31 * 86'400",
        "Ariel": "5, 0.01, (Color){188, 203, 205, 255}, 2.5204 * 86'400",
        "Umbriel": "5, 0.01, (Color){79, 85, 91, 255}, 4.1442 * 86'400",
        "Titania": "5, 0.01, (Color){150, 153, 160, 255}, 8.7059 * 86'400",
        "Oberon": "5, 0.01, (Color){102, 97, 92, 255}, 13.4632 * 86'400",
        "Miranda": "5, 0.01, (Color){169, 174, 163, 255}, 1.4135 * 86'400",
        "Triton": "5, 0.01, (Color){194, 190, 183, 255}, 5.8769 * 86'400,",
        "Nereid": "5, 0.01, (Color){164, 171, 174, 255}, 360.13 * 86'400",
        "Proteus": "5, 0.01, (Color){89, 91, 89, 255}, 1.1223 * 86'400",
        "Charon": "5, 0.01, (Color){126, 117, 109, 255}, 6.3872 * 86'400",
        "Nix": "5, 0.01, (Color){184, 183, 175, 255}, 24.856 * 86'400",
        "Hydra": "5, 0.01, (Color){164, 165, 157, 255}, 38.202 * 86'400",
        "Kerberos": "5, 0.01, (Color){84, 80, 78, 255}, 32.168 * 86'400",
        "Styx": "5, 0.01, (Color){123, 120, 113, 255}, 20.162 * 86'400"
    }

    planet_to_code: dict[str, str] = {planet: "" for planet in planet_to_horizon_id.keys()}
    
    base_coordinate_regex: str = " ?= ?((\\+|-)?\\d\\.(\\d)+(E(\\+|-)?(\\d)+)?)"
    gm_physical_regex: re.Pattern[str] = re.compile(
        r"""
        \bGM\b
        (?!\s*1[-\s]?sigma)             # skip the "GM 1-sigma" uncertainty line
        \s*(?:,\s*)?                    # optional comma: "GM, km^3/s^2"
        (?:                             # optional unit multiplier: "GM 10^-3 (km^3/s^2)"
            10\s*\^\s*(?P<unit_exp>[+-]?\d+)\s*
        )?
        (?:\(\s*[a-z ]+\s*\)\s*)?       # optional qualifier: "GM (planet) km^3/s^2"
        (?:\(\s*)?                      # optional opening parenthesis: "GM (km^3/s^2)"
        km\s*\^?\s*3                    # "km^3"
        \s*(?:/\s*s\s*\^?\s*2           # "/s^2"
           |s\s*\^?\s*-\s*2)            # or "km^3 s^-2"
        \s*\)?                          # optional closing parenthesis
        \s*=\s*~?\s*
        (?P<value>[+-]?(?:\d+(?:\.\d*)?|\.\d+)   # 869.326
            (?:[eEdD][+-]?\d+)?)                 # or 7.087546066894452E-04
        """,
        re.IGNORECASE | re.VERBOSE,
    )

    def convert_num(num: str, e_factor: int) -> str:
        """Convert from km to m; refactor ex. "E0+9" to "e9". (e_factor = 3 -> x 10^3 -> x1000)"""
    
        pre_e_part = num.split("E")[0]
        post_e_part = num.split("E")[1]
    
        return pre_e_part + "e" + str(int(post_e_part) + e_factor)  # add 4 because km -> m


    def parse_gm(physical: str) -> float | None:
        """GM in m^3/s^2 (== the C++ gravitational_mass), or None if not listed."""
        match = gm_physical_regex.search(physical)
        if match is None:
            return None

        value = float(match.group("value").replace("D", "E").replace("d", "E"))
        exp = int(match.group("unit_exp") or 0)

        return value * 10.0**exp * 1e9  # km^3/s^2 -> m^3/s^2

    for i, (planet_name, horizon_id) in enumerate(planet_to_horizon_id.items()):
        try:
            response = requests.get(
                f"https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND='{horizon_id}'&CENTER='@0'&MAKE_EPHEM='YES'&EPHEM_TYPE='VECTORS'&START_TIME='{start_date.strftime("%Y-%m-%d-%H-%M-%S")}'&STOP_TIME='{(start_date + datetime.timedelta(days=1)).strftime("%Y-%m-%d-%H-%M-%S")}'&STEP_SIZE='1d'&REF_SYSTEM='J2000'&REF_PLANE='ECLIPTIC'&OUT_UNITS='KM-S'&OBJ_DATA='YES'"
            )
            response.raise_for_status()

            result: str = response.text

            parts: list[str] = result.split(
                "*******************************************************************************"
            )

            try:
                physical_part = parts[1]
                coordinate_part = parts[8]
            except IndexError:
                print(f"index error, {result=}")
                raise

            x, y, z, vx, vy, vz, gm = [None for _ in range(7)]

            x_match = re.search("X" + base_coordinate_regex, coordinate_part)
            y_match = re.search("Y" + base_coordinate_regex, coordinate_part)
            z_match = re.search("Z" + base_coordinate_regex, coordinate_part)

            vx_match = re.search("VX" + base_coordinate_regex, coordinate_part)
            vy_match = re.search("VY" + base_coordinate_regex, coordinate_part)
            vz_match = re.search("VZ" + base_coordinate_regex, coordinate_part)

            if x_match: x = convert_num(x_match.group(1), 3)
            if y_match: y = convert_num(y_match.group(1), 3)
            if z_match: z = convert_num(z_match.group(1), 3)

            if vx_match: vx = convert_num(vx_match.group(1), 3)
            if vy_match: vy = convert_num(vy_match.group(1), 3)
            if vz_match: vz = convert_num(vz_match.group(1), 3)

            gm = parse_gm(physical_part)
            if gm is None:
                gm = hardcoded_gm.get(horizon_id)

            if gm is None:
                print(f"[WARNING] assuming gm=0 for planet {planet_name} ({horizon_id})")
                gm = 0.0

            if any(val is None for val in [x, y, z]):
                print(f"No match found for {x=}/{y=}/{z=} for planet {planet_name} ({horizon_id})")
            if any(val is None for val in [vx, vy, vz]):
                print(f"No match found for {vx=}/{vy=}/{vz=} for planet {planet_name} ({horizon_id})")
            if gm is None:
                print(f"No match found for {gm=} for planet {planet_name} ({horizon_id})")

            if any(val is None for val in [x, y, z, vx, vy, vz]):
                continue

            x = typing.cast(str, x)
            y = typing.cast(str, y)
            z = typing.cast(str, z)

            positions.append(Vec3(
                scientific_notation_to_float(x),
                scientific_notation_to_float(y),
                scientific_notation_to_float(z)
            ))

            planet_to_code[planet_name] = (
                f"{{\"{planet_name}\", {{{x}, {y}, {z}}}, {{{vx}, {vy}, {vz}}}, {gm}, "
                f"{fix_code[planet_name]}{oblateness_code(planet_name)}}},"
            )
        except Exception as e:
            print(f"Exception {e} occurred with planet {planet_name} ({horizon_id=})")
            raise
        finally:
            print(f"Fetched vectors of planet {planet_name} ({horizon_id=}) ({i + 1}/{len(planet_to_horizon_id.keys())})")
    
    for planet_name, code_line in planet_to_code.items():
        res.append(code_line)
    # res.append(sun)

    return res, positions

def print_code() -> None:
    start_values: tuple[list[str], list[Vec3]] = fetch_vectors(START)
    print(f"#define NUM_CELESTIAL_BODIES {len(start_values[0])}\n")
    print("CelestialBody simulation::celestial_bodies[NUM_CELESTIAL_BODIES]{")
    print("    std::array<CelestialBodyInit, NUM_CELESTIAL_BODIES>{{")
    for val in start_values[0]:
        print(f"        {val}")
    print("    }}")
    print("};")

def calculate_error_metrics(expected_pos: Vec3, candidate_pos: Vec3) -> ErrorMetrics:
    delta = candidate_pos - expected_pos
    distance_m = delta.length()

    return ErrorMetrics(
        distance_m=distance_m,
        relative_percent=distance_m / max(expected_pos.length(), EPS) * 100,
        angular_degrees=expected_pos.angle_degrees(candidate_pos),
        radial_m=candidate_pos.length() - expected_pos.length(),
    )

def print_one_error_metric(metrics: ErrorMetrics) -> None:
    print(
        f" distance = {metrics.distance_m / 1_000:.3f} km ({metrics.distance_m / AU:.6f} AU)\n"
        f" relative = {metrics.relative_percent:.3f}%\n"
        f" angle    = {metrics.angular_degrees:.3f} deg\n"
        f" radial   = {metrics.radial_m / 1_000:.3f} km ({metrics.radial_m / AU:.6f} AU)\n"
    )

def print_error_metric(
    expected_vectors: list[Vec3],
    candidate_vectors: list[Vec3],
    names: list[str],
    enabled_body_indices: frozenset[int],
) -> None:
    if len(expected_vectors) != len(candidate_vectors) or len(expected_vectors) != len(names):
        raise ValueError(
            "Expected vectors, candidate vectors, and body names must have equal lengths "
            f"({len(expected_vectors)}, {len(candidate_vectors)}, and {len(names)})"
        )

    if not enabled_body_indices:
        raise ValueError("At least one body must be enabled for error analysis")

    indices = {name: index for index, name in enumerate(names)}
    sun_index = indices["Sun"]
    sun_enabled = sun_index in enabled_body_indices
    expected_sun = expected_vectors[sun_index]
    candidate_sun = candidate_vectors[sun_index]

    if sun_enabled:
        print("Sun-relative errors (the Sun itself is measured in barycentric coordinates):\n")
    else:
        print("Barycentric errors (the Sun is disabled):\n")

    for index, (name, expected_pos, candidate_pos) in enumerate(zip(names, expected_vectors, candidate_vectors)):
        if index not in enabled_body_indices:
            continue

        if name == "Sun" or not sun_enabled:
            expected_rel = expected_pos
            candidate_rel = candidate_pos
        else:
            expected_rel = expected_pos - expected_sun
            candidate_rel = candidate_pos - candidate_sun

        print(f"{name} error:\n")
        print_one_error_metric(calculate_error_metrics(expected_rel, candidate_rel))

    print("Parent-relative moon errors:\n")

    for name, parent in moon_to_parent.items():
        moon_index = indices[name]
        parent_index = indices[parent]

        if moon_index not in enabled_body_indices or parent_index not in enabled_body_indices:
            continue

        expected_rel = expected_vectors[moon_index] - expected_vectors[parent_index]
        candidate_rel = candidate_vectors[moon_index] - candidate_vectors[parent_index]

        print(f"{name} error relative to {parent}:\n")
        print_one_error_metric(calculate_error_metrics(expected_rel, candidate_rel))

def ecliptic_pole_unit_vector(pole_ra_deg: float, pole_dec_deg: float) -> Vec3:
    """IAU J2000 equatorial pole (alpha0, delta0) -> unit spin axis in the J2000
    ECLIPTIC frame the simulation integrates in. Reproduces the pole_axis
    literals:
    Build the equatorial unit vector, then rotate equatorial -> ecliptic about the x-axis by the mean obliquity."""
    eps = math.radians(OBLIQUITY_J2000_DEG)
    a = math.radians(pole_ra_deg)
    d = math.radians(pole_dec_deg)

    xe = math.cos(d) * math.cos(a)
    ye = math.cos(d) * math.sin(a)
    ze = math.sin(d)

    return Vec3(
        xe,
        ye * math.cos(eps) + ze * math.sin(eps),
        -ye * math.sin(eps) + ze * math.cos(eps),
    )

def oblateness_code(planet_name: str) -> str:
    """C++ initializer suffix: ", j2, equatorial_radius, {pole_axis}" for the oblate planets in planet_oblateness
    Empty for every other body."""
    ob = planet_oblateness.get(planet_name)
    if ob is None:
        return ""

    k = ecliptic_pole_unit_vector(ob.pole_ra_deg, ob.pole_dec_deg)
    return (
        f", {ob.j2}, {ob.equatorial_radius_m}, "
        f"{{{k.x:.8f}, {k.y:.8f}, {k.z:.8f}}}"
    )

def analyze_error(compare_planetary_systems: bool = False) -> None:
    p_run = parse_program_output(read_program_output(2))
    target_epoch = START + datetime.timedelta(seconds=p_run.steps * p_run.dt)

    expected_positions: list[Vec3] = fetch_vectors(target_epoch)[1]
    names: list[str] = list(planet_to_horizon_id.keys())
    system_mode = compare_planetary_systems or p_run.body_set == "planet-systems"
    candidate_positions: list[Vec3] = p_run.vectors
    enabled_body_indices: frozenset[int] = p_run.enabled_body_indices

    if system_mode:
        expected_positions = collapse_system_positions(expected_positions, names)
        if p_run.body_set == "all":
            require_complete_explicit_systems(p_run.enabled_body_indices, names, "Program run")
            candidate_positions = collapse_system_positions(candidate_positions, names)
        enabled_body_indices = system_output_indices(enabled_body_indices, names)

    print("\n")
    print("Reproduce this report:")
    print(f"  Program invocation:  {p_run.invocation_command}")
    print(f"  Analysis invocation: {analysis_invocation_command()}\n")
    print(f"JPL simulation time: {(target_epoch - START).days / 365} years (@ 365 days)")
    print(f"Program simulation time: {p_run.years} years (@ 365 days)\n")
    print(f"Body set: {p_run.body_set}")
    print(f"Analyzed representation: {'planetary-system barycentres' if system_mode else 'body centres'}\n")

    enabled_names = [names[index] for index in sorted(enabled_body_indices)]
    print(f"Enabled bodies: {', '.join(enabled_names)}\n")

    print(f"JPL positions: {expected_positions}")
    print(f"Program positions: {candidate_positions}\n")

    print_error_metric(expected_positions, candidate_positions, names, enabled_body_indices)

def compare(compare_planetary_systems: bool = False) -> None:
    reference_run = parse_program_output(pathlib.Path(sys.argv[2]).read_text())
    candidate_run = parse_program_output(read_program_output(3))

    names: list[str] = list(planet_to_horizon_id.keys())
    system_mode = compare_planetary_systems or candidate_run.body_set == "planet-systems"

    print("\n")
    print("Reproduce this report:")
    print(f"  Reference invocation: {reference_run.invocation_command}")
    print(f"  Candidate invocation: {candidate_run.invocation_command}")
    print(f"  Analysis invocation:  {analysis_invocation_command()}\n")
    print(f"Reference simulation time: {reference_run.years} years (@ 365 days)")
    print(f"Candidate simulation time: {candidate_run.years} years (@ 365 days)\n")

    if reference_run.years != candidate_run.years:
        print("Simulation times do not match (years)")
        sys.exit(1)
    if reference_run.steps * reference_run.dt != candidate_run.steps * candidate_run.dt:
        print("Simulation times do not match (steps * dt)")
        sys.exit(1)
    if not len(reference_run.vectors) == len(candidate_run.vectors) == len(names):
        print("Length of vectors & names do not match")
        sys.exit(1)

    reference_vectors: list[Vec3] = reference_run.vectors
    candidate_vectors: list[Vec3] = candidate_run.vectors
    reference_enabled_indices: frozenset[int] = reference_run.enabled_body_indices
    candidate_enabled_indices: frozenset[int] = candidate_run.enabled_body_indices
    if system_mode:
        if reference_run.body_set != "all":
            raise RuntimeError("Planetary-system comparison requires an explicit --body-set all reference run")
        require_complete_explicit_systems(reference_enabled_indices, names, "Reference run")
        reference_vectors = collapse_system_positions(reference_vectors, names)
        reference_enabled_indices = system_output_indices(reference_enabled_indices, names)
        if candidate_run.body_set == "all":
            require_complete_explicit_systems(candidate_enabled_indices, names, "Candidate run")
            candidate_vectors = collapse_system_positions(candidate_vectors, names)
        candidate_enabled_indices = system_output_indices(candidate_enabled_indices, names)

    missing_reference_indices = candidate_enabled_indices - reference_enabled_indices
    if missing_reference_indices:
        missing_names = [names[index] for index in sorted(missing_reference_indices)]
        print(f"Reference run does not enable candidate bodies: {', '.join(missing_names)}")
        sys.exit(1)

    reference_enabled_names = [names[index] for index in sorted(reference_enabled_indices)]
    candidate_enabled_names = [names[index] for index in sorted(candidate_enabled_indices)]
    print(f"Analyzed representation: {'planetary-system barycentres' if system_mode else 'body centres'}")
    print(f"Reference enabled bodies: {', '.join(reference_enabled_names)}")
    print(f"Candidate enabled bodies: {', '.join(candidate_enabled_names)}\n")

    print(f"Reference positions: {reference_vectors}\n")
    print(f"Candidate positions: {candidate_vectors}\n")

    print_error_metric(
        reference_vectors,
        candidate_vectors,
        names,
        candidate_enabled_indices,
    )

    print(f"Reference computation time: {reference_run.computation_seconds} seconds")
    print(f"Candidate computation time: {candidate_run.computation_seconds} seconds\n")
    if candidate_run.computation_seconds > 0.0:
        print(f"Speedup: {reference_run.computation_seconds / candidate_run.computation_seconds}x")
    else:
        print("Speedup: unavailable (candidate runtime rounded to 0.00 seconds)")

def compare_csv(compare_planetary_systems: bool = False) -> None:
    reference_path: pathlib.Path = pathlib.Path(sys.argv[2])
    candidate_path: pathlib.Path = pathlib.Path(sys.argv[3])

    class RunningStats:
        def __init__(self) -> None:
            self.count = 0
            self.final = 0.0
            self.maximum_absolute = 0.0
            self.sum_of_squares = 0.0

        def add(self, value: float) -> None:
            self.count += 1
            self.final = value
            self.maximum_absolute = max(self.maximum_absolute, abs(value))
            self.sum_of_squares += value * value

        def rms(self) -> float:
            if self.count == 0:
                return float("nan")
            return math.sqrt(self.sum_of_squares / self.count)

    def relative_state(
        sample: CSVSample,
        body: str,
        use_sun_relative_frame: bool,
        require_explicit_system: bool,
    ) -> BodyState:
        if compare_planetary_systems and body in planetary_system_to_moons:
            state = weighted_system_state(sample, body, require_explicit_system)
        else:
            state = sample.bodies[body]

        if body in moon_to_parent:
            origin_name = moon_to_parent[body]
        elif body != "Sun" and use_sun_relative_frame:
            origin_name = "Sun"
        else:
            return state

        if origin_name not in sample.bodies:
            raise RuntimeError(
                f"{body} requires {origin_name} for relative analysis at "
                f"simulated_seconds={sample.simulated_seconds}"
            )

        if compare_planetary_systems and origin_name in planetary_system_to_moons:
            origin = weighted_system_state(sample, origin_name, require_explicit_system)
        else:
            origin = sample.bodies[origin_name]
        return BodyState(
            state.position - origin.position,
            state.velocity - origin.velocity,
        )

    def ensure_finite(state: BodyState, body: str, sample: CSVSample, source: str) -> None:
        values = (
            state.position.x,
            state.position.y,
            state.position.z,
            state.velocity.x,
            state.velocity.y,
            state.velocity.z,
        )
        if not all(math.isfinite(value) for value in values):
            raise RuntimeError(
                f"Non-finite {source} state for {body} at "
                f"simulated_seconds={sample.simulated_seconds}"
            )

    def print_distance_stats(label: str, stats: RunningStats) -> None:
        print(
            f"  {label}: final={stats.final / 1_000:.6f} km, "
            f"max_abs={stats.maximum_absolute / 1_000:.6f} km, "
            f"RMS={stats.rms() / 1_000:.6f} km"
        )

    def print_phase_stats(label: str, stats: RunningStats) -> None:
        print(
            f"  {label}: final={stats.final:.6f} deg, "
            f"max_abs={stats.maximum_absolute:.6f} deg, "
            f"RMS={stats.rms():.6f} deg"
        )

    reference_samples = read_csv_samples(reference_path)
    candidate_samples = read_csv_samples(candidate_path)
    if not reference_samples:
        raise RuntimeError(f"Reference CSV contains no samples: {reference_path}")
    if not candidate_samples:
        raise RuntimeError(f"Candidate CSV contains no samples: {candidate_path}")

    reference_bodies = set(reference_samples[0].bodies)
    candidate_bodies = set(candidate_samples[0].bodies)
    missing_reference_bodies = candidate_bodies - reference_bodies
    if missing_reference_bodies:
        raise RuntimeError(
            "Reference CSV is missing candidate bodies: "
            + ", ".join(sorted(missing_reference_bodies))
        )

    if compare_planetary_systems:
        system_names: set[str] = {"Sun", "Mercury", "Venus", *planetary_system_to_moons.keys()}
        body_order: list[str] = [
            name
            for name in planet_to_horizon_id
            if name in candidate_bodies and name in system_names
        ]
        for parent in planetary_system_to_moons:
            if parent not in candidate_bodies:
                continue
            missing_members: list[str] = [
                name
                for name in planetary_system_members(parent)
                if name not in reference_bodies
            ]
            if missing_members:
                raise RuntimeError(
                    f"Explicit reference CSV cannot construct the {parent} system; "
                    f"missing: {', '.join(missing_members)}"
                )
    else:
        body_order = [name for name in planet_to_horizon_id if name in candidate_bodies]
    position_error_stats: dict[str, RunningStats] = {body: RunningStats() for body in body_order}
    normalized_position_error_stats: dict[str, RunningStats] = {body: RunningStats() for body in body_order}
    normalization_unavailable: dict[str, str] = {
        body: "no Sun/parent-relative reference distance (barycentric position only)"
        for body in body_order
        if body == "Sun" or (body not in moon_to_parent and "Sun" not in candidate_bodies)
    }
    moon_component_stats: dict[str, dict[str, RunningStats]] = {
        body: {
            "radial": RunningStats(),
            "along-track": RunningStats(),
            "cross-track": RunningStats(),
            "phase": RunningStats(),
        }
        for body in body_order
        if body in moon_to_parent
    }
    previous_wrapped_phase: dict[str, float] = {}
    unwrapped_phase: dict[str, float] = {}

    sample_count: int = 0
    previous_simulated_seconds: int | None = None
    for reference_sample, candidate_sample in itertools.zip_longest(
        reference_samples,
        candidate_samples,
    ):
        if reference_sample is None:
            raise RuntimeError("Candidate CSV contains more samples than the reference CSV")
        if candidate_sample is None:
            raise RuntimeError("Reference CSV contains more samples than the candidate CSV")
        if reference_sample.simulated_seconds != candidate_sample.simulated_seconds:
            raise RuntimeError(
                "CSV sample times do not match: "
                f"reference={reference_sample.simulated_seconds}, "
                f"candidate={candidate_sample.simulated_seconds}"
            )
        if (
            previous_simulated_seconds is not None
            and reference_sample.simulated_seconds <= previous_simulated_seconds
        ):
            raise RuntimeError(
                "CSV sample times must be strictly increasing; found "
                f"{reference_sample.simulated_seconds} after {previous_simulated_seconds}"
            )

        if set(reference_sample.bodies) != reference_bodies:
            raise RuntimeError("Reference CSV body columns changed between samples")
        if set(candidate_sample.bodies) != candidate_bodies:
            raise RuntimeError("Candidate CSV body columns changed between samples")

        previous_simulated_seconds = reference_sample.simulated_seconds
        sample_count += 1

        for body in body_order:
            use_sun_relative_frame: bool = "Sun" in candidate_bodies
            reference_state: BodyState = relative_state(
                reference_sample,
                body,
                use_sun_relative_frame,
                require_explicit_system=compare_planetary_systems,
            )
            candidate_state: BodyState = relative_state(
                candidate_sample,
                body,
                use_sun_relative_frame,
                require_explicit_system=False,
            )
            ensure_finite(reference_state, body, reference_sample, "reference")
            ensure_finite(candidate_state, body, candidate_sample, "candidate")

            position_error: Vec3 = candidate_state.position - reference_state.position
            position_error_m: float = position_error.length()
            position_error_stats[body].add(position_error_m)

            reference_radius: float = reference_state.position.length()
            if body not in normalization_unavailable:
                if not math.isfinite(reference_radius) or reference_radius <= EPS:
                    # Never clamp the denominator or silently summarize only some samples.
                    normalization_unavailable[body] = (
                        f"zero/near-zero or non-finite reference distance at "
                        f"simulated_seconds={reference_sample.simulated_seconds}"
                    )
                else:
                    # Normalize each sample before aggregating: RMS(e_k / |q_ref,k|).
                    normalized_position_error: float = position_error_m / reference_radius
                    if not math.isfinite(normalized_position_error):
                        raise RuntimeError(
                            f"Non-finite normalized position error for {body} at "
                            f"simulated_seconds={reference_sample.simulated_seconds}"
                        )
                    normalized_position_error_stats[body].add(normalized_position_error)

            if body not in moon_component_stats:
                continue

            reference_angular_momentum: Vec3 = reference_state.position.cross(reference_state.velocity)
            angular_momentum_length: float = reference_angular_momentum.length()
            candidate_radius: float = candidate_state.position.length()
            if reference_radius <= EPS or angular_momentum_length <= EPS or candidate_radius <= EPS:
                raise RuntimeError(
                    f"Cannot construct the RTN frame for {body} at "
                    f"simulated_seconds={reference_sample.simulated_seconds}"
                )

            radial_axis: Vec3 = reference_state.position / reference_radius
            normal_axis: Vec3 = reference_angular_momentum / angular_momentum_length
            along_track_axis: Vec3 = normal_axis.cross(radial_axis)

            moon_component_stats[body]["radial"].add(position_error.dot(radial_axis))
            moon_component_stats[body]["along-track"].add(position_error.dot(along_track_axis))
            moon_component_stats[body]["cross-track"].add(position_error.dot(normal_axis))

            wrapped_phase: float = math.atan2(
                normal_axis.dot(reference_state.position.cross(candidate_state.position)),
                reference_state.position.dot(candidate_state.position),
            )
            if body not in previous_wrapped_phase:
                unwrapped_phase[body] = wrapped_phase
            else:
                phase_change = wrapped_phase - previous_wrapped_phase[body]
                if phase_change > math.pi:
                    phase_change -= 2.0 * math.pi
                elif phase_change < -math.pi:
                    phase_change += 2.0 * math.pi
                unwrapped_phase[body] += phase_change

            previous_wrapped_phase[body] = wrapped_phase
            moon_component_stats[body]["phase"].add(math.degrees(unwrapped_phase[body]))

    print("\nReproduce this report:")
    print(f"  Reference CSV:      {shlex.quote(str(reference_path))}")
    print(f"  Candidate CSV:      {shlex.quote(str(candidate_path))}")
    print(f"  Analysis invocation: {analysis_invocation_command()}\n")
    print(f"Samples compared: {sample_count}")
    print(f"Final simulated time: {previous_simulated_seconds} seconds")
    print(f"Analyzed representation: {'planetary-system barycentres' if compare_planetary_systems else 'body centres'}")
    print(f"Candidate bodies: {', '.join(body_order)}\n")

    print("Time-series position errors:")
    print("Normalized position = position error / reference distance at each sample (dimensionless).")
    for body in body_order:
        if compare_planetary_systems and body in planetary_system_to_moons:
            frame = "system barycentre relative to Sun" if "Sun" in candidate_bodies else "system barycentre"
        elif body in moon_to_parent:
            frame = f"relative to {moon_to_parent[body]}"
        elif body != "Sun" and "Sun" in candidate_bodies:
            frame = "relative to Sun"
        else:
            frame = "barycentric"
        print(f"{body} ({frame}):")
        print_distance_stats("position", position_error_stats[body])
        if body in normalization_unavailable:
            print(f"  normalized position: unavailable ({normalization_unavailable[body]})")
        else:
            stats = normalized_position_error_stats[body]
            print(
                f"  normalized position (dimensionless): final={stats.final:.6e}, "
                f"max={stats.maximum_absolute:.6e}, RMS={stats.rms():.6e}"
            )

    if moon_component_stats:
        print("\nParent-relative moon RTN and unwrapped phase errors:")
        for body in body_order:
            if body not in moon_component_stats:
                continue
            stats = moon_component_stats[body]
            print(f"{body} relative to {moon_to_parent[body]}:")
            print_distance_stats("radial", stats["radial"])
            print_distance_stats("along-track", stats["along-track"])
            print_distance_stats("cross-track", stats["cross-track"])
            print_phase_stats("unwrapped phase", stats["phase"])


def repeated_runs() -> None:
    folder_path: pathlib.Path = pathlib.Path(sys.argv[2])

    if not folder_path.is_dir(follow_symlinks=False):
        raise RuntimeError(f"Path is not a folder: {folder_path}")

    computations_times: list[float] = []

    for i, file in enumerate(folder_path.iterdir()):
        if file.is_file():
            text = file.read_text()

            match = re.search(r"Computation time:\s*([\d.]+)\s*seconds", text)

            if match:
                computation_time = float(match.group(1))
                computations_times.append(computation_time)

    if not computations_times:
        raise RuntimeError("No files match the computation-time RegEx.")
    else:
        print(f"Average computation time of {len(computations_times)} runs: {sum(computations_times) / len(computations_times)}")


def print_usage() -> None:
    print("Commands:")
    print(f"  {sys.argv[0]} code                                               Formatted celestial body JPL data")
    print(f"  {sys.argv[0]} analyze output.txt                                 Analyze body centres against JPL; planet-systems runs are detected automatically")
    print(f"  {sys.argv[0]} analyze-systems output.txt                         Analyze modeled planetary-system barycentres against constructed JPL barycentres")
    print(f"  {sys.argv[0]} compare reference.txt candidate.txt                Compare 2 program endpoints; planet-systems candidates are detected automatically")
    print(f"  {sys.argv[0]} compare-systems reference.txt candidate.txt        Compare planetary-system barycentres for 2 program endpoints")
    print(f"  {sys.argv[0]} compare-csv reference.csv candidate.csv            Compare body-centre time series (absolute and normalized final/max/RMS)")
    print(f"  {sys.argv[0]} compare-systems-csv reference.csv candidate.csv    Compare planetary-system-barycentre time series (absolute and normalized final/max/RMS)")
    print(f"  {sys.argv[0]} repeated-runs folder/                              Analyze and clalculate the average computation time of multiple runs")

def require_argc(required_argc: int) -> None:
    if len(sys.argv) < required_argc:
        print_usage()
        sys.exit(1)

if __name__ == '__main__':
    if len(sys.argv) < 2: print_usage(); sys.exit(0)
    match sys.argv[1]:
        case "code": require_argc(2); print_code()
        case "analyze": require_argc(2); analyze_error()
        case "analyze-systems": require_argc(2); analyze_error(compare_planetary_systems=True)
        case "compare": require_argc(3); compare()
        case "compare-systems": require_argc(3); compare(compare_planetary_systems=True)
        case "compare-csv": require_argc(4); compare_csv()
        case "compare-systems-csv": require_argc(4); compare_csv(compare_planetary_systems=True)
        case "repeated-runs": require_argc(3); repeated_runs()
        case _: print_usage(); sys.exit(1)
