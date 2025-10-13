import json
import re
import statistics
from typing import Dict, List, Tuple, Any

# Configuration for different route sets
ROUTE_SETS = {
    "ONE-201": {
        "FILTER": "ONE-201",
        "END_DWELL": 300,
        "EXCLUDED_BLOCKS": {141, 150}.union({i for i in range(300, 324)}),
        "COLOR": (0, 255, 255),
    },
    "WEST-201": {
        "FILTER": "WEST-201",
        "END_DWELL": 180,
        "EXCLUDED_BLOCKS": {316, 140, 111, 109}
        .union({i for i in range(321, 345)})
        .union({i for i in range(142, 209)})
        .union({i for i in range(307, 315)}),
        "COLOR": (0, 255, 0),
    },
    "STH-201": {
        "FILTER": "STH-201",
        "END_DWELL": 180,
        "EXCLUDED_BLOCKS": {
            208,
            206,
            187,
            189,
            173,
            175,
            169,
            161,
            141,
            341,
            342,
            315,
        }
        .union({i for i in range(321, 345)})
        .union({i for i in range(100, 139)}),
        "COLOR": (255, 0, 0),
    },
    "EAST-201": {
        "FILTER": "EAST-201",
        "END_DWELL": 180,
        "EXCLUDED_BLOCKS": {316, 158, 172, 169, 161, 320}
        .union({i for i in range(300, 314)})
        .union({i for i in range(100, 158)})
        .union({i for i in range(172, 209)}),
        "COLOR": (255, 128, 0),
    },
}

version = "AKL_V1_1_0"


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


def tblock(block_num: int) -> int:
    """Map block number based on version"""
    global version
    if version == "AKL_V1_0_0":
        if 304 <= block_num <= 344 or 138 <= block_num <= 208:
            return block_num - 1
    return block_num


def process_route_set(
    set_name: str,
    config: Dict[str, Any],
    data: Dict[str, Any],
    output_file: Any,
    version=version,
) -> List[str]:
    """Process a single route set and write to output file"""
    filter_str: str = config["FILTER"]
    end_dwell: int = config["END_DWELL"]
    route_excluded_blocks: set[int] = config["EXCLUDED_BLOCKS"]
    color: Tuple[int, int, int] = config.get("COLOR", (255, 255, 255))

    route_classes: List[str] = []

    # Process each schedule that matches this filter
    for schedule_key, entry in data.items():
        if filter_str not in schedule_key:
            continue

        trip_excluded_blocks = set[int]()
        trip_excluded_blocks = route_excluded_blocks

        # Generate class name
        class_name = sanitize_class_name(schedule_key)

        # Extract data
        start_times = entry.get("start_times", [])
        blocks_times = entry.get("blocks_times", {})

        max_samples = 0

        # Calculate averages
        averages: List[Tuple[float, int]] = []
        for block, times in blocks_times.items():
            if times:
                # avg = sum(times) / len(times)
                avg = statistics.median(times)

                averages.append((avg, int(block)))

                if len(times) > max_samples:
                    max_samples = len(times)

        if max_samples <= 1:
            print(f"Excluding {schedule_key} as it only has {max_samples} sample")
            continue

        # Sort by time to maintain sequence
        averages.sort(key=lambda x: x[0], reverse=False)

        block_order = []

        if filter_str == "WEST-201":
            block_order = (
                list(range(100, 142))  # 100-141
                + list(range(305, 300, -1))  # 305-301 (descending)
                + list(range(320, 314, -1))  # 320-315 (descending)
            )
        elif filter_str == "STH-201":
            # If the averages[i] include a Manukau branch block (343, 344) use that order
            # if any(block in (343, 344) for _, block in averages):
            #     print(f"Excluding {schedule_key} as it has Manukau blocks")
            #     continue

            # else:
            block_order = (
                list(range(208, 139, -1))  # 208-140 (descending)
                + list(range(305, 300, -1))  # 305-301 (descending)
                + list(range(320, 314, -1))  # 320-315 (descending)
            )
        elif filter_str == "ONE-201":
            block_order = list(range(156, 139, -1))  # 208-140 (descending)

        elif filter_str == "EAST-201":
            block_order = (
                list(range(344, 341, -1))  # 344-342 (descending)
                + list(range(171, 158, -1))  # 171-159 (descending)
                + list(range(341, 314, -1))  # 341-315 (descending)
            )

        start_blocks = {b for _, b in averages[:5]}
        end_blocks = {b for _, b in averages[-5:]}

        # Determine block order and whether to use it
        block_order, custom_order_enabled = determine_block_order(
            schedule_key, block_order, start_blocks, end_blocks
        )

        if custom_order_enabled:
            # Create a mapping for fast lookup
            order_map = {block: i for i, block in enumerate(block_order)}

            # Sort using the custom order
            averages.sort(key=lambda x: order_map.get(x[1], float("inf")))

        # Find last valid average (excluding certain blocks)
        valid_averages = [
            (avg, block) for avg, block in averages if block not in trip_excluded_blocks
        ]

        MIN_VALID_BLOCKS = 10
        if len(valid_averages) < MIN_VALID_BLOCKS:
            # Skip this schedule if not enough valid averages
            print(f"Excluding {schedule_key} as it has {len(valid_averages)} block/s")
            continue
        else:
            route_classes.append(class_name)

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
        start_times.sort()
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
            next_block = averages[i + 1][1] if i < len(averages) - 1 else None
            tweak_time = False
            exclude = False

            # Constrain to -100, 10,000
            if avg < -100 or avg > 10000:
                exclude = True

            if prev_avg is not None:
                if avg <= prev_avg:
                    tweak_time = True

                if next_avg is not None:
                    if (
                        avg > prev_avg
                        and avg > next_avg
                        and prev_avg + (19 * 2) < next_avg
                        and next_block not in trip_excluded_blocks
                    ):
                        tweak_time = True

                if avg > prev_avg + 1000 and prev_avg > 0:
                    exclude = True

            if block in trip_excluded_blocks or exclude:
                output_file.write(f"\t\t//{{ {int(avg)}, {tblock(block)} }},\n")
                averages.pop(i)
            elif tweak_time:
                adjusted_avg = (
                    (prev_avg + next_avg) / 2
                    if next_avg is not None and next_avg > prev_avg
                    else prev_avg + 19
                )
                if adjusted_avg > prev_avg:
                    output_file.write(
                        f"\t\t{{ {int(adjusted_avg)}, {tblock(block)} }}, // Was: {int(avg)} ({int(adjusted_avg - avg):+d} seconds)\n"
                    )
                    averages[i] = (adjusted_avg, block)
                    i += 1
                else:
                    output_file.write(
                        f"\t\t//{{ {int(avg)}, {tblock(block)} }}, Failed to adjust: {int(adjusted_avg)}\n"
                    )
                    averages.pop(i)
            elif (
                i == 0
                and next_avg is not None
                and next_avg < 1000
                and block_order[0] not in trip_excluded_blocks
            ):
                if block == block_order[0] or block != block_order[1]:
                    output_file.write(
                        f"\t\t{{ 0, {tblock(block)} }}, // Was: {int(avg)}\n"
                    )
                    averages[i] = (0, block)
                else:
                    output_file.write(
                        f"\t\t{{ 0, {tblock(block_order[0])} }}, // Added\n"
                    )
                    output_file.write(f"\t\t{{ {int(avg)}, {tblock(block)} }},\n")
                i += 1
            else:
                output_file.write(f"\t\t{{ {int(avg)}, {tblock(block)} }},\n")
                i += 1

        # Add end entry
        output_file.write(f"		{{ {int(end_time)}, -1 }}\n")

        output_file.write("	};\n")
        output_file.write("};\n\n")

    return route_classes


def determine_block_order(
    schedule_key: str, block_order: List[int], start_blocks: set, end_blocks: set
) -> Tuple[List[int], bool]:
    """Determine if custom block order should be used and whether to reverse it."""
    if not block_order:
        return block_order, False

    # Check if any of the first five blocks match the start or end blocks
    first_five_match_start = any(block in start_blocks for block in block_order[:5])
    first_five_match_end = any(block in end_blocks for block in block_order[:5])

    if first_five_match_start:
        return block_order, True
    elif first_five_match_end:
        return list(reversed(block_order)), True
    else:
        print(f"Unexpected Starting Blocks for {schedule_key}")
        return block_order, False


def generate_cpp_header(
    filename: str = "block_schedules.json", version: str = "AKL_V1_1_0"
):
    """Generate C++ header file from block schedules JSON with multiple route sets"""

    with open(filename, "r") as f:
        data = json.load(f)

    # Start writing the header file
    output_file = f"include/{version}_Timetable.h"
    with open(output_file, "w") as f:

        all_route_classes = []

        f.write("// Auto-generated by createHeaderFile.py\n")

        # Process each route set
        for set_name, config in ROUTE_SETS.items():
            print(f"\nProcessing {set_name} routes...")
            route_classes = process_route_set(set_name, config, data, f, version)
            all_route_classes.extend(route_classes)
            print(f"Generated {len(route_classes)} routes for {set_name}")

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
    versions = ["AKL_V1_0_0", "AKL_V1_1_0"]
    for version in versions:
        generate_cpp_header(
            "Timetable Generator/AKL/block_schedule.json",
            version,
        )
