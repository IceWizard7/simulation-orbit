import requests
import re

planet_to_horizon_id: dict[str, int] = {
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

fix_code: dict[str, str] = {
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

sun: str = "CelestialBody sun   = {\"Sun\", {0, 0, 0}, {0, 0, 0}, 1.98847e30, 10, 0.03, (Color){255, 230,  40, 255}, 30'000, 100'000};"

planet_to_code: dict[str, str] = {planet: "" for planet in planet_to_horizon_id.keys()}

BASE_COORDINATE_REGEX: str = " ?= ?((\\+|-)?\\d\\.(\\d)+(E(\\+|-)?(\\d)+)?)"
MASS_PHYSICAL_REGEX: str = "Mass x *10\\^(\\d+) \\(kg\\) *= *([0-9-.]+)"


def convert_num(num: str, e_factor: int) -> str:
    """Convert from km to m; refactor ex. "E0+9" to "e9". (e_factor = 3 -> x 10^3 -> x1000)"""

    pre_e_part = num.split("E")[0]
    post_e_part = num.split("E")[1]

    return pre_e_part + "e" + str(int(post_e_part) + e_factor)  # add 4 because km -> m


for planet_name, horizon_id in planet_to_horizon_id.items():
    result: str = str(
        requests.get(
            f"https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND='{horizon_id}'&CENTER='@0'&MAKE_EPHEM='YES'&EPHEM_TYPE='VECTORS'&START_TIME='2026-06-26'&STOP_TIME='2026-06-27'&STEP_SIZE='1d'&REF_SYSTEM='J2000'&REF_PLANE='ECLIPTIC'&OUT_UNITS='KM-S'&OBJ_DATA='YES'"
        ).content
    )

    parts: list[str] = result.split(
        "*******************************************************************************"
    )

    physical_part = parts[1]
    coordinate_part = parts[8]

    x = convert_num(re.search("X" + BASE_COORDINATE_REGEX, coordinate_part).group(1), 3)
    y = convert_num(re.search("Y" + BASE_COORDINATE_REGEX, coordinate_part).group(1), 3)
    z = convert_num(re.search("Z" + BASE_COORDINATE_REGEX, coordinate_part).group(1), 3)
    vx = convert_num(
        re.search("VX" + BASE_COORDINATE_REGEX, coordinate_part).group(1), 3
    )
    vy = convert_num(
        re.search("VY" + BASE_COORDINATE_REGEX, coordinate_part).group(1), 3
    )
    vz = convert_num(
        re.search("VZ" + BASE_COORDINATE_REGEX, coordinate_part).group(1), 3
    )
    mass = convert_num(
        re.search(MASS_PHYSICAL_REGEX, physical_part).group(2)
        + "E+"
        + re.search(MASS_PHYSICAL_REGEX, physical_part).group(1),
        0,
    )

    planet_to_code[planet_name] = (
        f"CelestialBody {planet_name.lower()} = {{\"{planet_name}\", {{{x}, {y}, {z}}}, {{{vx}, {vy}, {vz}}}, {mass}, {fix_code[planet_name]}}};"
    )

for planet_name, code_line in planet_to_code.items():
    print(code_line)
print(sun)
