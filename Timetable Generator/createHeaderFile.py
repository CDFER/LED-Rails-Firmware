import json
import re
import statistics
from typing import Dict, List, Tuple, Any

# STATION_BLOCKS = {100, 101, 102, 103, 104}
STATION_BLOCKS = {}

# Configuration for different route sets
ROUTE_SETS = {
    "JVL": {
        "FILTER": "JVL",
        "END_DWELL": 300,
        "EXCLUDED_BLOCKS": {101, 102, 103, 104},
        "COLOR": (0, 191, 191),
    },
    "HVL": {
        "FILTER": "HVL",
        "END_DWELL": 60,
        "EXCLUDED_BLOCKS": {100, 101, 103, 104, 107, 162, 230}.union(
            {i for i in range(193, 234)}
        ),
        "COLOR": (255, 96, 0),
    },
    "KPL": {
        "FILTER": "KPL",
        "END_DWELL": 300,
        "EXCLUDED_BLOCKS": {100, 102, 103, 104, 107, 215, 228}.union(
            i for i in range(116, 192)
        ),
        "COLOR": (159, 223, 0),
    },
    "MEL": {
        "FILTER": "MEL",
        "END_DWELL": 180,
        "EXCLUDED_BLOCKS": {100, 101, 102, 104, 107},
        "COLOR": (255, 96, 128),
    },
    "WRL": {
        "FILTER": "WRL",
        "END_DWELL": 300,
        "EXCLUDED_BLOCKS": {100, 101, 102, 103, 106, 161},
        "COLOR": (255, 143, 0),
    },
}

VERSION = "WLG_V1_0_0"


def sanitize_class_name(name: str) -> str:
    """Convert schedule key to valid C++ class name"""
    # Replace invalid characters with underscores
    sanitized = re.sub(r"[^a-zA-Z0-9_]", "_", name)
    # Ensure it doesn't start with a number
    if sanitized and sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized


def seconds_to_time_string(seconds: int) -> str:
    """Convert seconds to HH:MM:SS format for comment"""
    hours = seconds // 3600
    minutes = (seconds % 3600) // 60
    secs = seconds % 60
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"


def process_route_set(
    set_name: str, config: Dict[str, Any], data: Dict[str, Any], output_file: Any
) -> List[str]:
    """Process a single route set and write to output file"""
    filter_str: str = config["FILTER"]
    end_dwell: int = config["END_DWELL"]
    excluded_blocks: set[int] = config["EXCLUDED_BLOCKS"]
    color: Tuple[int, int, int] = config.get("COLOR", (255, 255, 255))

    route_classes: List[str] = []

    # Process each schedule that matches this filter
    for schedule_key, entry in data.items():
        if filter_str not in schedule_key:
            continue

        # Generate class name
        class_name = sanitize_class_name(schedule_key)
        route_classes.append(class_name)

        # Extract data
        start_times = entry.get("start_times", [])
        blocks_times = entry.get("blocks_times", {})

        # Calculate averages
        averages: List[Tuple[float, int]] = []
        for block, times in blocks_times.items():
            if times:
                # avg = sum(times) / len(times)
                avg = statistics.median(times)

                averages.append((avg, int(block)))

                if int(block) in STATION_BLOCKS:
                    print(f"{schedule_key} Block:{block} {len(times)} times")

        increasing = "__0_" in schedule_key

        # Sort by block number to maintain sequence
        averages.sort(key=lambda x: x[1], reverse=not increasing)

        # Find last valid average (excluding certain blocks)
        valid_averages = [
            (avg, block) for avg, block in averages if block not in excluded_blocks
        ]
        if valid_averages:
            last_valid_avg = max(avg for avg, block in valid_averages)
            end_time = int(last_valid_avg) + end_dwell
        else:
            end_time = end_dwell

        # Write class definition
        output_file.write(f"class {class_name} : public TrainRoute {{\n")
        output_file.write("  public:\n")
        output_file.write(
            "	const std::vector<TimetableEntry>& getEntries() const override {\n"
        )
        output_file.write("		return timetable;\n")
        output_file.write("	}\n\n")

        # output_file.write("	const char* getRouteName() const override {\n")
        # output_file.write(f'		return "{schedule_key}";\n')
        # output_file.write("	}\n\n")

        output_file.write("\tCRGB getColor() const override {\n")
        output_file.write(f"\t\treturn CRGB({color[0]}, {color[1]}, {color[2]});\n")
        output_file.write("\t}\n\n")

        output_file.write(
            "	const std::vector<uint32_t>& getStartTimes() const override {\n"
        )
        output_file.write("		static const std::vector<uint32_t> startTimes = {")

        # Format start times on one line if few items, otherwise multiple lines
        if len(start_times) <= 12:
            output_file.write(f" {', '.join(str(int(s)) for s in start_times)} ")
        else:
            output_file.write("\n")
            for i, start_time in enumerate(start_times):
                if i % 12 == 0 and i > 0:
                    output_file.write("\n")
                if i % 12 == 0:
                    output_file.write("			")
                output_file.write(f"{int(start_time)}")
                if i < len(start_times) - 1:
                    output_file.write(", ")
            output_file.write("\n		")
        output_file.write("};\n")
        output_file.write("		return startTimes;\n")
        output_file.write("	}\n\n")

        output_file.write("  private:\n")
        output_file.write(
            "	static const inline std::vector<TimetableEntry> timetable = {"
        )

        # Add comment with first start time
        if start_times:
            first_time_str = seconds_to_time_string(int(start_times[0]))
            output_file.write(f"  // First departure: {first_time_str}")
            if len(start_times) > 1:
                interval = int(start_times[1] - start_times[0])
                output_file.write(f", interval ~{interval}s")

        output_file.write("\n")

        # Process averages to ensure strictly increasing times
        i = 0
        while i < len(averages):
            avg, block = averages[i]
            prev_avg = averages[i - 1][0] if i > 0 else None
            next_avg = averages[i + 1][0] if i < len(averages) - 1 else None
            tweak_time = False
            exclude = False

            if prev_avg is not None:
                if avg <= prev_avg:
                    tweak_time = True

                if avg > prev_avg + 1000 and prev_avg > 0:
                    exclude = True

            if block in excluded_blocks or exclude:
                output_file.write(f"\t\t//{{ {int(avg)}, {block} }},\n")
                averages.pop(i)
            elif tweak_time:
                adjusted_avg = (
                    (prev_avg + next_avg) / 2
                    if next_avg is not None and next_avg > prev_avg
                    else prev_avg + 20
                )
                if adjusted_avg > prev_avg:
                    output_file.write(
                        f"\t\t{{ {int(adjusted_avg)}, {block} }}, // Was: {int(avg)}\n"
                    )
                    averages[i] = (adjusted_avg, block)
                    i += 1
                else:
                    output_file.write(
                        f"\t\t//{{ {int(avg)}, {block} }}, Failed to adjust: {int(adjusted_avg)}\n"
                    )
                    averages.pop(i)
            else:
                output_file.write(f"\t\t{{ {int(avg)}, {block} }},\n")
                i += 1

        # Add end entry
        output_file.write(f"		{{ {int(end_time)}, -1 }}\n")

        output_file.write("	};\n")
        output_file.write("};\n\n")

    return route_classes


def generate_cpp_header(
    filename: str = "block_schedules.json", output_file: str = f"{VERSION}_Timetable.h"
):
    """Generate C++ header file from block schedules JSON with multiple route sets"""

    with open(filename, "r") as f:
        data = json.load(f)

    # Start writing the header file
    with open(output_file, "w") as f:

        all_route_classes = []

        f.write("// Auto-generated by createHeaderFile.py\n")

        # Process each route set
        for set_name, config in ROUTE_SETS.items():
            print(f"Processing {set_name} routes...")
            route_classes = process_route_set(set_name, config, data, f)
            all_route_classes.extend(route_classes)
            print(f"  Generated {len(route_classes)} routes for {set_name}")

        # Write getAllRoutes function
        f.write("// === Global List of Routes ===\n")
        f.write("inline const std::vector<const TrainRoute*> getAllRoutes() {\n")
        f.write("	static const std::vector<const TrainRoute*> routes = {\n")
        for class_name in all_route_classes:
            f.write(f"		new {class_name}(),\n")
        f.write("	};\n")
        f.write("	return routes;\n")
        f.write("}\n\n")

    print(f"\nGenerated {output_file} with {len(all_route_classes)} total routes")


if __name__ == "__main__":
    # Generate the header file
    generate_cpp_header(
        "Timetable Generator/block_schedules.json", f"include/{VERSION}_Timetable.h"
    )
