from __future__ import annotations
import sys
import dataclasses
import requests
import re
import datetime
import math
import pathlib
import typing

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

# GM in m^3/s^2
# Phobos/Deimos: JPL satellite solution values (Horizons lists only Mass for 401/402).
# Nereid: no mass or GM anywhere in Horizons
# => this is a density-based estimate (~3.1e19 kg), fine since Nereid is tiny
hardcoded_gm: dict[int, float] = {
    401: 7.087546066894452e-4 * 1e9,  # Phobos ≈ 7.0875e5 m^3/s^2
    402: 9.615569648120313e-5 * 1e9,  # Deimos ≈ 9.6156e4 m^3/s^2
    802: 2.07e9,                      # Nereid ≈ 3.1e19 kg * G
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

    def __sub__(self, other: Vec3) -> Vec3:
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)

    def length(self) -> float:
        return math.sqrt(self.x ** 2 + self.y ** 2 + self.z ** 2)

    def dot(self, other: Vec3) -> float:
        return (self.x * other.x) + (self.y * other.y) + (self.z * other.z)

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
    invocation_command: str

@dataclasses.dataclass(frozen=True)
class ErrorMetrics:
    distance_m: float
    relative_percent: float
    angular_degrees: float
    radial_m: float

def parse_program_output(text: str) -> ProgramRun:
    computation_seconds_match = re.search(r"Computation time:\s*([0-9.]+)", text)
    simulated_years_match = re.search(r"Simulation time:\s*([0-9.]+)", text)
    steps_match = re.search(r"Steps simulated:\s*(\d+)", text)
    dt_match = re.search(r"Time step:\s*([0-9.eE+-]+)", text)
    invocation_match = re.search(r"Invocation command:\s*([A-Za-z\s]+)", text)

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
    invocation_command: str = invocation_match.group(1)

    positions_line = next(line for line in text.splitlines() if "|" in line and line.startswith("["))
    vectors = []
    for raw in re.findall(r"\[([^]]+)]", positions_line):
        x, y, z = (float(part.strip()) for part in raw.split(","))
        vectors.append(Vec3(x, y, z))

    return ProgramRun(computation_seconds, simulated_years, steps, dt, vectors, invocation_command)

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

def fetch_vectors(start_date: datetime.date) -> tuple[list[str], list[Vec3]]:
    res: list[str] = []
    positions: list[Vec3] = []

    fix_code: dict[str, str] = {
        "Sun": "10, 0.03, (Color){255, 230,  40, 255}, 30'000, 100'000, \"resources/sun.jpg\"",
        "Mercury": "5, 0.01, (Color){150, 150, 150, 255}, 40'000, 1'000, \"resources/mercury.jpg\"",
        "Venus": "5, 0.01, (Color){245, 190,  70, 255}, 30'000, 10'000, \"resources/venus.jpg\"",
        "Earth": "5, 0.01, (Color){ 40, 120, 204, 255}, 20'000, 10'000, \"resources/earth.jpg\"",
        "Mars": "10, 0.02, (Color){220,  60,  40, 255}, 15'000, 10'000, \"resources/mars.jpg\"",
        "Jupiter": "10, 0.15, (Color){220, 150,  85, 255}, 4'000, 10'000, \"resources/jupiter.jpg\"",
        "Saturn": "10, 0.15, (Color){235, 205, 120, 255}, 4'000, 50'000, \"resources/saturn.jpg\"",
        "Uranus": "10, 0.15, (Color){ 80, 220, 220, 255}, 4'000, 50'000, \"resources/uranus.jpg\"",
        "Neptune": "10, 0.15, (Color){ 40,  80, 230, 255}, 4'000, 50'000, \"resources/neptune.jpg\"",
        "Pluto": "10, 0.15, (Color){185, 155, 130, 255}, 4'000, 100'000",
        "Moon": "5, 0.01, (Color){200, 200, 200, 255}, 40'000, 1'000",
        "Phobos": "5, 0.01, (Color){105, 93, 82, 255}, 40'000, 1'000",
        "Deimos": "5, 0.01, (Color){139, 118, 99, 255}, 40'000, 1'000",
        "Io": "5, 0.01, (Color){240, 197, 79, 255}, 40'000, 1'000",
        "Europa": "5, 0.01, (Color){210, 199, 174, 255}, 40'000, 1'000",
        "Ganymede": "5, 0.01, (Color){139, 126, 112, 255}, 40'000, 1'000",
        "Callisto": "5, 0.01, (Color){76, 66, 60, 255}, 40'000, 1'000",
        "Mimas": "5, 0.01, (Color){207, 208, 197, 255}, 40'000, 1'000",
        "Enceladus": "5, 0.01, (Color){231, 240, 242, 255}, 40'000, 1'000",
        "Tethys": "5, 0.01, (Color){202, 203, 191, 255}, 40'000, 1'000",
        "Dione": "5, 0.01, (Color){190, 190, 180, 255}, 40'000, 1'000",
        "Rhea": "5, 0.01, (Color){180, 180, 170, 255}, 40'000, 1'000",
        "Titan": "5, 0.01, (Color){201, 141, 71, 255}, 40'000, 1'000",
        "Hyperion": "5, 0.01, (Color){136, 99, 70, 255}, 40'000, 1'000",
        "Iapetus": "5, 0.01, (Color){154, 137, 108, 255}, 40'000, 1'000",
        "Phoebe": "5, 0.01, (Color){71, 68, 67, 255}, 40'000, 1'000",
        "Ariel": "5, 0.01, (Color){188, 203, 205, 255}, 40'000, 1'000",
        "Umbriel": "5, 0.01, (Color){79, 85, 91, 255}, 40'000, 1'000",
        "Titania": "5, 0.01, (Color){150, 153, 160, 255}, 40'000, 1'000",
        "Oberon": "5, 0.01, (Color){102, 97, 92, 255}, 40'000, 1'000",
        "Miranda": "5, 0.01, (Color){169, 174, 163, 255}, 40'000, 1'000",
        "Triton": "5, 0.01, (Color){194, 190, 183, 255}, 40'000, 1'000",
        "Nereid": "5, 0.01, (Color){164, 171, 174, 255}, 40'000, 1'000",
        "Proteus": "5, 0.01, (Color){89, 91, 89, 255}, 40'000, 1'000",
        "Charon": "5, 0.01, (Color){126, 117, 109, 255}, 40'000, 1'000",
        "Nix": "5, 0.01, (Color){184, 183, 175, 255}, 40'000, 1'000",
        "Hydra": "5, 0.01, (Color){164, 165, 157, 255}, 40'000, 1'000",
        "Kerberos": "5, 0.01, (Color){84, 80, 78, 255}, 40'000, 1'000",
        "Styx": "5, 0.01, (Color){123, 120, 113, 255}, 40'000, 1'000"
    }
    
    # sun: str = "CelestialBody sun   = {\"Sun\", {0, 0, 0}, {0, 0, 0}, 1.98847e30, 10, 0.03, (Color){255, 230,  40, 255}, 30'000, 100'000};"
    
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
                f"https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND='{horizon_id}'&CENTER='@0'&MAKE_EPHEM='YES'&EPHEM_TYPE='VECTORS'&START_TIME='{start_date.strftime("%Y-%m-%d")}'&STOP_TIME='{(start_date + datetime.timedelta(days=1)).strftime("%Y-%m-%d")}'&STEP_SIZE='1d'&REF_SYSTEM='J2000'&REF_PLANE='ECLIPTIC'&OUT_UNITS='KM-S'&OBJ_DATA='YES'"
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
                f"{{\"{planet_name}\", {{{x}, {y}, {z}}}, {{{vx}, {vy}, {vz}}}, {gm}, {fix_code[planet_name]}}},"
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
    print("CelestialBody simulation::celestial_bodies[NUM_CELESTIAL_BODIES] = {")
    for val in start_values[0]:
        print(f"    {val}")
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

def print_error_metric(expected_vectors: list[Vec3], candidate_vectors: list[Vec3], names: list[str]) -> None:
    if len(expected_vectors) != len(candidate_vectors) or len(expected_vectors) != len(names):
        raise ValueError(
            "Expected vectors, candidate vectors, and body names must have equal lengths "
            f"({len(expected_vectors)}, {len(candidate_vectors)}, and {len(names)})"
        )

    indices = {name: index for index, name in enumerate(names)}
    expected_sun = expected_vectors[indices["Sun"]]
    candidate_sun = candidate_vectors[indices["Sun"]]

    print("Sun-relative errors (the Sun itself is measured in barycentric coordinates):\n")

    for name, expected_pos, candidate_pos in zip(names, expected_vectors, candidate_vectors):
        if name == "Sun":
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

        expected_rel = expected_vectors[moon_index] - expected_vectors[parent_index]
        candidate_rel = candidate_vectors[moon_index] - candidate_vectors[parent_index]

        print(f"{name} error relative to {parent}:\n")
        print_one_error_metric(calculate_error_metrics(expected_rel, candidate_rel))

def analyze_error() -> None:
    p_run = parse_program_output(read_program_output(2))
    target_epoch = START + datetime.timedelta(seconds=p_run.steps * p_run.dt)

    expected_positions: list[Vec3] = fetch_vectors(target_epoch)[1]
    names: list[str] = list(planet_to_horizon_id.keys())

    print("\n")
    print(f"Invocation command: {p_run.invocation_command}\n")
    print(f"JPL simulation time: {(target_epoch - START).days / 365} years (@ 365 days)")
    print(f"Program simulation time: {p_run.years} years (@ 365 days)\n")

    print(f"JPL positions: {expected_positions}")
    print(f"Program positions: {p_run.vectors}\n")

    print_error_metric(expected_positions, p_run.vectors, names)

def compare() -> None:
    reference_run = parse_program_output(pathlib.Path(sys.argv[2]).read_text())
    candidate_run = parse_program_output(read_program_output(3))

    names: list[str] = list(planet_to_horizon_id.keys())

    print("\n")
    print(f"Reference invocation command: {reference_run.invocation_command}\n")
    print(f"Candidate invocation command: {candidate_run.invocation_command}\n")
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

    print(f"Reference positions: {reference_run.vectors}\n")
    print(f"Candidate positions: {candidate_run.vectors}\n")

    print_error_metric(reference_run.vectors, candidate_run.vectors, names)

    print(f"Reference computation time: {reference_run.computation_seconds} seconds")
    print(f"Candidate computation time: {candidate_run.computation_seconds} seconds\n")
    print(f"Speedup: {reference_run.computation_seconds / candidate_run.computation_seconds}x")

def print_usage() -> None:
    print("Commands:")
    print(f"  {sys.argv[0]} code                            Formatted celestial body JPL data")
    print(f"  {sys.argv[0]} analyze output                  Analyze program output & compare to JPL data. Reads output from stdin if output is not given")
    print(f"  {sys.argv[0]} compare reference candidate     Compare 2 program outputs. Reads from candidate from stdin if candidate is not given")

def require_argc(required_argc: int) -> None:
    if len(sys.argv) < required_argc:
        print_usage()
        sys.exit(1)

if __name__ == '__main__':
    if len(sys.argv) < 2: print_usage(); sys.exit(0)
    match sys.argv[1]:
        case "code": require_argc(2); print_code()
        case "analyze": require_argc(2); analyze_error()
        case "compare": require_argc(3); compare()
        case _: print_usage()
