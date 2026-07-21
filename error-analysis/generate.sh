#!/usr/bin/env bash
cd "$(dirname "$0")/.." || exit 1

./cmake-build-release/simulation-orbit --headless --dt 300 --years 200 --body-set planets      | python3 analyze.py analyze > error-analysis/300_200_planets.txt
./cmake-build-release/simulation-orbit --headless --dt 300 --years 200 --body-set all          | python3 analyze.py analyze > error-analysis/300_200_all.txt
./cmake-build-release/simulation-orbit --headless --dt 300 --years 200 --body-set planets --j2 | python3 analyze.py analyze > error-analysis/300_200_planets_j2.txt
./cmake-build-release/simulation-orbit --headless --dt 300 --years 200 --body-set all --j2     | python3 analyze.py analyze > error-analysis/300_200_all_j2.txt
./cmake-build-release/simulation-orbit --headless --dt 900 --years 200 --body-set planets      | python3 analyze.py analyze > error-analysis/900_200_planets.txt
./cmake-build-release/simulation-orbit --headless --dt 900 --years 200 --body-set all          | python3 analyze.py analyze > error-analysis/900_200_all.txt
./cmake-build-release/simulation-orbit --headless --dt 900 --years 200 --body-set planets --j2 | python3 analyze.py analyze > error-analysis/900_200_planets_j2.txt
./cmake-build-release/simulation-orbit --headless --dt 900 --years 200 --body-set all --j2     | python3 analyze.py analyze > error-analysis/900_200_all_j2.txt
./cmake-build-release/simulation-orbit --headless --dt 25  --years 200 --body-set all --j2     | python3 analyze.py analyze > error-analysis/25_200_all_j2.txt