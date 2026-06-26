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
    print(
        re.search(MASS_PHYSICAL_REGEX, physical_part).group(1)
        + re.search(MASS_PHYSICAL_REGEX, physical_part).group(2)
    )
    mass = ""

    planet_to_code[planet_name] = (
        f"CelestialBody {planet_name.lower()} = {{{{{x}, {y}, {z}}}, {{{vx}, {vy}, {vz}}}, {mass}}};"
    )

for planet_name, code_line in planet_to_code.items():
    print(code_line)
