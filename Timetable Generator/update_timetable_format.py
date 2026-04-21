"""
Updates C++ timetable header files to use RouteSpan instead of std::vector,
and static const instances instead of heap-allocated (new) objects.
"""

import re
import os

FILES = [
    r"include\MEL_V1_0_0_Timetable.h",
    r"include\AKL_V1_0_0_Timetable.h",
    r"include\AKL_V1_1_0_Timetable.h",
    r"include\WLG_V1_0_0_Timetable.h",
]

def transform(content: str) -> str:
    # 1. Remove #include <vector>
    content = content.replace("#include <vector>\n", "")

    # 2. getEntries() return type: vector ref -> RouteSpan
    content = content.replace(
        "const std::vector<TimetableEntry>& getEntries() const override",
        "RouteSpan<TimetableEntry> getEntries() const override",
    )

    # 3. getStartTimes() return type: vector ref -> RouteSpan
    content = content.replace(
        "const std::vector<uint32_t>& getStartTimes() const override",
        "RouteSpan<uint32_t> getStartTimes() const override",
    )

    # 4. startTimes storage: vector -> constexpr array
    content = content.replace(
        "static const std::vector<uint32_t> startTimes = {",
        "static constexpr uint32_t startTimes[] = {",
    )

    # 5. timetable storage: vector -> constexpr array
    #    Handle with and without trailing comment

    content = re.sub(
        r"static const inline std::vector<TimetableEntry> timetable = \{(.*)",
        r"static inline constexpr TimetableEntry timetable[] = {\1",
        content,
    )

    # 6. Transform getAllRoutes(): extract class names, create static instances,
    #    replace new with address-of, change return type to RouteSpan
    match = re.search(
        r"(// === Global List of Routes ===\n)"
        r"inline const std::vector<const TrainRoute\*> getAllRoutes\(\) \{\n"
        r"\tstatic const std::vector<const TrainRoute\*> routes = \{\n"
        r"(.*?)"
        r"\t\};\n"
        r"\treturn routes;\n"
        r"\}",
        content,
        re.DOTALL,
    )

    if not match:
        print("  WARNING: Could not find getAllRoutes() function!")
        return content

    comment = match.group(1)
    body = match.group(2)

    # Extract class names from "new ClassName()" or "new ClassName(),"
    class_names = re.findall(r"new (\w+)\(\)", body)

    # Build static instance declarations
    static_instances = "\n".join(
        f"static const {cls} inst_{cls};" for cls in class_names
    )

    # Build pointer array entries
    pointer_entries = "\n".join(
        f"\t\t&inst_{cls}," for cls in class_names
    )
    # Remove trailing comma from last entry
    pointer_entries = pointer_entries.rsplit(",", 1)[0]

    replacement = (
        f"{static_instances}\n\n"
        f"{comment}"
        f"inline RouteSpan<const TrainRoute*> getAllRoutes() {{\n"
        f"\tstatic const TrainRoute* const routes[] = {{\n"
        f"{pointer_entries}\n"
        f"\t}};\n"
        f"\treturn routes;\n"
        f"}}"
    )

    content = content[: match.start()] + replacement + content[match.end() :]

    return content


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    for filepath in FILES:
        full_path = os.path.join(script_dir, filepath)
        print(f"Processing {filepath}...")

        with open(full_path, "r", encoding="utf-8") as f:
            content = f.read()

        content = transform(content)

        with open(full_path, "w", encoding="utf-8") as f:
            f.write(content)

        print(f"  Done.")

    print("\nAll files updated.")


if __name__ == "__main__":
    main()
