import sys
import dataclasses
import requests
import re
import datetime
import math

planet_to_horizon_id: dict[str, int] = {
    "Sun": 10,
    "Mercury": 199,
    "Venus": 299,
    "Earth": 399,
    "Mars": 499,
    "Jupiter": 599,
    "Saturn": 699,
    "Uranus": 799,
    "Neptune": 899,
    "Pluto": 999,
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
        "Sun": "10, 0.03, (Color){255, 230,  40, 255}, 30'000, 100'000",
        "Mercury": "5, 0.01, (Color){150, 150, 150, 255}, 40'000, 1'000",
        "Venus": "5, 0.01, (Color){245, 190,  70, 255}, 30'000, 10'000",
        "Earth": "5, 0.01, (Color){ 40, 120, 204, 255}, 20'000, 10'000",
        "Mars": "10, 0.02, (Color){220,  60,  40, 255}, 15'000, 10'000",
        "Jupiter": "10, 0.15, (Color){220, 150,  85, 255}, 4'000, 10'000",
        "Saturn": "10, 0.15, (Color){235, 205, 120, 255}, 4'000, 50'000",
        "Uranus": "10, 0.15, (Color){ 80, 220, 220, 255}, 4'000, 50'000",
        "Neptune": "10, 0.15, (Color){ 40,  80, 230, 255}, 4'000, 50'000",
        "Pluto": "10, 0.15, (Color){185, 155, 130, 255}, 4'000, 100'000",
    }
    
    # sun: str = "CelestialBody sun   = {\"Sun\", {0, 0, 0}, {0, 0, 0}, 1.98847e30, 10, 0.03, (Color){255, 230,  40, 255}, 30'000, 100'000};"
    
    planet_to_code: dict[str, str] = {planet: "" for planet in planet_to_horizon_id.keys()}
    
    base_coordinate_regex: str = " ?= ?((\\+|-)?\\d\\.(\\d)+(E(\\+|-)?(\\d)+)?)"
    # mass_physical_regex: str = "Mass x *10\\^(\\d+) \\(kg\\) *= *([0-9-.]+)"
    mass_physical_regex: str = "Mass( x|, ) *10\\^(\\d+) (\\(kg\\)|kg) *= *~?([0-9-.]+)"

    def convert_num(num: str, e_factor: int) -> str:
        """Convert from km to m; refactor ex. "E0+9" to "e9". (e_factor = 3 -> x 10^3 -> x1000)"""
    
        pre_e_part = num.split("E")[0]
        post_e_part = num.split("E")[1]
    
        return pre_e_part + "e" + str(int(post_e_part) + e_factor)  # add 4 because km -> m
    
    
    for planet_name, horizon_id in planet_to_horizon_id.items():
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

        x = convert_num(re.search("X" + base_coordinate_regex, coordinate_part).group(1), 3)
        y = convert_num(re.search("Y" + base_coordinate_regex, coordinate_part).group(1), 3)
        z = convert_num(re.search("Z" + base_coordinate_regex, coordinate_part).group(1), 3)
        vx = convert_num(
            re.search("VX" + base_coordinate_regex, coordinate_part).group(1), 3
        )
        vy = convert_num(
            re.search("VY" + base_coordinate_regex, coordinate_part).group(1), 3
        )
        vz = convert_num(
            re.search("VZ" + base_coordinate_regex, coordinate_part).group(1), 3
        )
        mass = convert_num(
            re.search(mass_physical_regex, physical_part).group(4)
            + "E+"
            + re.search(mass_physical_regex, physical_part).group(2),
            0,
        )

        positions.append(Vec3(
            scientific_notation_to_float(x),
            scientific_notation_to_float(y),
            scientific_notation_to_float(z)
        ))
    
        planet_to_code[planet_name] = (
            f"CelestialBody {planet_name.lower()} = {{\"{planet_name}\", {{{x}, {y}, {z}}}, {{{vx}, {vy}, {vz}}}, {mass}, {fix_code[planet_name]}}};"
        )
    
    for planet_name, code_line in planet_to_code.items():
        res.append(code_line)
    # res.append(sun)

    return res, positions

def get_code() -> None:
    start_values: tuple[list[str], list[Vec3]] = compute_vectors(START)
    print(f"Code: \n\n{'\n'.join(start_values[0])}")

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

    expected_sun: Vec3 = expected_positions[0]
    given_sun: Vec3 = given_positions[1]

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
