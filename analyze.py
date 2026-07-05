import sys
import dataclasses
import requests
import re
import datetime
import math
import pathlib

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
    "Kerberos": 904,
    # "Styx": 905  # Doesn't have data
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

def read_program_output() -> str:
    if len(sys.argv) >= 3:
        return pathlib.Path(sys.argv[2]).read_text()

    if not sys.stdin.isatty():
        text = sys.stdin.read()
        if text.strip():
            return text

    raise SystemExit(
        "Usage:\n"
        "  python3 analyze.py analyze program-output.txt\n"
        "or:\n"
        "  ./cmake-build-release/simulation-orbit | python3 analyze.py analyze"
    )

def parse_program_output(text: str) -> tuple[float, int | None, float | None, list[Vec3]]:
    years = float(re.search(r"Simulation time:\s*([0-9.]+)", text).group(1))

    steps_match = re.search(r"Steps simulated:\s*(\d+)", text)
    dt_match = re.search(r"Time step:\s*([0-9.eE+-]+)", text)

    steps = int(steps_match.group(1)) if steps_match else None
    dt = float(dt_match.group(1)) if dt_match else None

    positions_line = next(line for line in text.splitlines() if "|" in line and line.startswith("["))
    vectors = []
    for raw in re.findall(r"\[([^]]+)]", positions_line):
        x, y, z = (float(part.strip()) for part in raw.split(","))
        vectors.append(Vec3(x, y, z))

    return years, steps, dt, vectors


def compute_vectors(start_date: datetime.date) -> tuple[list[str], list[Vec3]]:
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
    mass_physical_regex: re.Pattern[str] = re.compile(
        r"""
        \bMass\b
        \s*(?:,\s*)?              # optional comma: "Mass, x10^22"
        (?:\(\s*)?                # optional opening parenthesis: "Mass (10^20 kg)"
        x?\s*10\s*\^\s*
        (?P<unit_exp>[+-]?\d+)    # 20, 24, 22
        \s*(?:\(\s*kg\s*\)|kg)    # "(kg)" or "kg"
        \s*\)?                    # optional closing parenthesis
        \s*=\s*~?\s*
        (?P<value>[+-]?(?:\d+(?:\.\d*)?|\.\d+))
        (?:                       # optional value multiplier: "(10^-4)"
            \s*\(\s*10\s*\^\s*
            (?P<value_exp>[+-]?\d+)
            \s*\)
        )?
        """,
        re.IGNORECASE | re.VERBOSE,
    )

    def convert_num(num: str, e_factor: int) -> str:
        """Convert from km to m; refactor ex. "E0+9" to "e9". (e_factor = 3 -> x 10^3 -> x1000)"""
    
        pre_e_part = num.split("E")[0]
        post_e_part = num.split("E")[1]
    
        return pre_e_part + "e" + str(int(post_e_part) + e_factor)  # add 4 because km -> m
    
    
    for planet_name, horizon_id in planet_to_horizon_id.items():
        try:
            result: str = str(
                requests.get(
                    f"https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND='{horizon_id}'&CENTER='@0'&MAKE_EPHEM='YES'&EPHEM_TYPE='VECTORS'&START_TIME='{start_date.strftime("%Y-%m-%d")}'&STOP_TIME='{(start_date + datetime.timedelta(days=1)).strftime("%Y-%m-%d")}'&STEP_SIZE='1d'&REF_SYSTEM='J2000'&REF_PLANE='ECLIPTIC'&OUT_UNITS='KM-S'&OBJ_DATA='YES'"
                ).content
            )

            parts: list[str] = result.split(
                "*******************************************************************************"
            )

            try:
                physical_part = parts[1]
                coordinate_part = parts[8]
            except IndexError:
                print(f"index error, {result=}")
                raise

            x, y, z, vx, vy, vz, mass = [None for _ in range(7)]

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

            mass_match = mass_physical_regex.search(physical_part)
            if mass_match:
                exponent = int(mass_match.group("unit_exp")) + int(mass_match.group("value_exp") or 0)

                mass = convert_num(
                    f'{mass_match.group("value")}E{exponent:+d}',
                    0,
                )

            if any(val is None for val in [x, y, z]):
                print(f"No match found for {x=}/{y=}/{z=} for planet {planet_name} ({horizon_id})")
            if any(val is None for val in [vx, vy, vz]):
                print(f"No match found for {vx=}/{vy=}/{vz=} for planet {planet_name} ({horizon_id})")
            if mass is None:
                print(f"No match found for {mass=} for planet {planet_name} ({horizon_id})")

            if any(val is None for val in [x, y, z, vx, vy, vz]):
                continue

            if mass is None:
                mass = 0
                # TODO: We assume mass=0 if we reach this
                print(f"[WARNING] assuming mass=0 for planet {planet_name} ({horizon_id})")

            positions.append(Vec3(
                scientific_notation_to_float(x),
                scientific_notation_to_float(y),
                scientific_notation_to_float(z)
            ))

            planet_to_code[planet_name] = (
                f"{{\"{planet_name}\", {{{x}, {y}, {z}}}, {{{vx}, {vy}, {vz}}}, {mass}, {fix_code[planet_name]}}},"
            )
        except Exception as e:
            print(f"Exception {e} occurred with planet {planet_name} ({horizon_id=})")
            raise
    
    for planet_name, code_line in planet_to_code.items():
        res.append(code_line)
    # res.append(sun)

    return res, positions

def get_code() -> None:
    start_values: tuple[list[str], list[Vec3]] = compute_vectors(START)
    print(f"#define NUM_CELESTIAL_BODIES {len(start_values[0])}\n")
    print("    CelestialBody celestial_bodies[NUM_CELESTIAL_BODIES] = {")
    for val in start_values[0]:
        print(f"        {val}")
    print("    };")

def analyze_error() -> None:
    years, steps, dt, given_positions = parse_program_output(read_program_output())
    if steps is not None and dt is not None:
        target_epoch = START + datetime.timedelta(seconds=steps * dt)
    else:
        target_epoch = START + datetime.timedelta(seconds=round(years * SECONDS_PER_YEAR))

    expected_positions: list[Vec3] = compute_vectors(target_epoch)[1]
    names: list[str] = list(planet_to_horizon_id.keys())

    print("\n")
    print(f"JPL simulation time: {(target_epoch - START).days / 365} years (@ 365 days)")
    print(f"Program simulation time: {years}\n")

    print(f"JPL positions: {expected_positions}")
    print(f"Program positions: {given_positions}\n")

    expected_sun: Vec3 = expected_positions[names.index("Sun")]
    given_sun: Vec3 = given_positions[names.index("Sun")]

    for name, expected_pos, given_pos in zip(names, expected_positions, given_positions):
        if name == "Sun":
            expected_rel: Vec3 = expected_pos
            given_rel: Vec3 = given_pos
        else:
            expected_rel = expected_pos - expected_sun
            given_rel = given_pos - given_sun

        delta: Vec3 = given_rel - expected_rel

        abs_error_m = delta.length()
        abs_error_km = delta.length() / 1_000
        abs_error_au = delta.length() / AU

        rel_error = abs_error_m / max(expected_rel.length(), EPS) * 100
        angular_error = expected_rel.angle_degrees(given_rel)
        radial_error_au = (given_rel.length() - expected_rel.length()) / AU

        print(
            f"{name} error:\n"
            f" distance = {abs_error_km:.0f} km ({abs_error_au:.6f} AU)\n"
            f" relative = {rel_error:.3f}%\n"
            f" angle    = {angular_error:.3f} deg\n"
            f" radial   = {radial_error_au:.6f} AU\n"
        )

def print_usage() -> None:
    print(f"Usage: {sys.argv[0]} [analyze|code]")

if __name__ == '__main__':
    if len(sys.argv) < 2: print_usage(); sys.exit(1)
    if sys.argv[1] == "analyze": analyze_error()
    if sys.argv[1] == "code": get_code()
