import csv
from html import escape
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BEFORE_PATH = ROOT / "build" / "performance_samples.csv"
AFTER_PATH = ROOT / "build" / "performance_samples_after_opt.csv"
OUT_DIR = ROOT / "build" / "performance_charts"


def read_rows(path):
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8-sig") as file:
        return list(csv.DictReader(file))


def to_int(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def find(rows, stage, row_count=None):
    matches = [row for row in rows if row.get("stage") == stage]
    if row_count is not None:
        matches = [row for row in matches if row.get("row_count") == str(row_count)]
    if not matches:
        return None
    return to_int(matches[-1].get("elapsed_ms"))


def svg_wrap(width, height, body):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<style>
text {{ font-family: Arial, "Microsoft YaHei", sans-serif; fill: #202124; }}
.title {{ font-size: 20px; font-weight: 700; }}
.axis {{ font-size: 12px; fill: #5f6368; }}
.label {{ font-size: 12px; }}
.value {{ font-size: 11px; fill: #3c4043; }}
.grid {{ stroke: #e8eaed; stroke-width: 1; }}
.axis-line {{ stroke: #9aa0a6; stroke-width: 1; }}
</style>
<rect width="100%" height="100%" fill="#ffffff"/>
{body}
</svg>
"""


def bar_chart(path, title, labels, series, width=980, height=560, y_label="elapsed_ms"):
    margin = {"left": 78, "right": 35, "top": 70, "bottom": 120}
    plot_w = width - margin["left"] - margin["right"]
    plot_h = height - margin["top"] - margin["bottom"]
    max_v = max([value for _, values, _ in series for value in values] + [1])
    y_max = max_v * 1.18
    parts = [f'<text x="{width / 2}" y="34" text-anchor="middle" class="title">{escape(title)}</text>']

    for index in range(6):
        value = y_max * index / 5
        y = margin["top"] + plot_h - (value / y_max) * plot_h
        parts.append(f'<line x1="{margin["left"]}" y1="{y:.1f}" x2="{width - margin["right"]}" y2="{y:.1f}" class="grid"/>')
        parts.append(f'<text x="{margin["left"] - 10}" y="{y + 4:.1f}" text-anchor="end" class="axis">{int(value)}</text>')

    parts.append(f'<line x1="{margin["left"]}" y1="{margin["top"]}" x2="{margin["left"]}" y2="{margin["top"] + plot_h}" class="axis-line"/>')
    parts.append(f'<line x1="{margin["left"]}" y1="{margin["top"] + plot_h}" x2="{width - margin["right"]}" y2="{margin["top"] + plot_h}" class="axis-line"/>')
    parts.append(f'<text x="20" y="{margin["top"] + plot_h / 2}" transform="rotate(-90 20 {margin["top"] + plot_h / 2})" text-anchor="middle" class="axis">{escape(y_label)}</text>')

    group_w = plot_w / len(labels)
    bar_gap = 8
    bar_w = min(38, (group_w - 24) / max(1, len(series)) - bar_gap)
    for label_index, label in enumerate(labels):
        group_x = margin["left"] + label_index * group_w
        total_bar_w = len(series) * bar_w + (len(series) - 1) * bar_gap
        start_x = group_x + (group_w - total_bar_w) / 2
        for series_index, (_, values, color) in enumerate(series):
            value = values[label_index]
            bar_h = (value / y_max) * plot_h if y_max else 0
            x = start_x + series_index * (bar_w + bar_gap)
            y = margin["top"] + plot_h - bar_h
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{bar_h:.1f}" fill="{color}" rx="2"/>')
            parts.append(f'<text x="{x + bar_w / 2:.1f}" y="{y - 5:.1f}" text-anchor="middle" class="value">{value}</text>')
        parts.append(f'<text x="{group_x + group_w / 2:.1f}" y="{height - 82}" text-anchor="middle" class="axis">{escape(label)}</text>')

    legend_x = margin["left"]
    legend_y = height - 34
    for name, _, color in series:
        parts.append(f'<rect x="{legend_x}" y="{legend_y - 11}" width="14" height="14" fill="{color}" rx="2"/>')
        parts.append(f'<text x="{legend_x + 20}" y="{legend_y}" class="label">{escape(name)}</text>')
        legend_x += 140

    path.write_text(svg_wrap(width, height, "\n".join(parts)), encoding="utf-8")


def line_chart(path, title, rows, stages, width=980, height=560):
    margin = {"left": 78, "right": 155, "top": 70, "bottom": 80}
    plot_w = width - margin["left"] - margin["right"]
    plot_h = height - margin["top"] - margin["bottom"]
    counts = sorted({to_int(row.get("row_count")) for row in rows if row.get("stage") in stages and row.get("row_count")})
    data = {stage: [find(rows, stage, count) or 0 for count in counts] for stage in stages}
    max_v = max([value for values in data.values() for value in values] + [1])
    y_max = max_v * 1.18
    colors = ["#1a73e8", "#188038", "#f29900", "#d93025", "#9334e6", "#00897b"]
    parts = [f'<text x="{width / 2}" y="34" text-anchor="middle" class="title">{escape(title)}</text>']

    for index in range(6):
        value = y_max * index / 5
        y = margin["top"] + plot_h - (value / y_max) * plot_h
        parts.append(f'<line x1="{margin["left"]}" y1="{y:.1f}" x2="{width - margin["right"]}" y2="{y:.1f}" class="grid"/>')
        parts.append(f'<text x="{margin["left"] - 10}" y="{y + 4:.1f}" text-anchor="end" class="axis">{int(value)}</text>')

    parts.append(f'<line x1="{margin["left"]}" y1="{margin["top"]}" x2="{margin["left"]}" y2="{margin["top"] + plot_h}" class="axis-line"/>')
    parts.append(f'<line x1="{margin["left"]}" y1="{margin["top"] + plot_h}" x2="{width - margin["right"]}" y2="{margin["top"] + plot_h}" class="axis-line"/>')
    for index, count in enumerate(counts):
        x = margin["left"] + (index / max(1, len(counts) - 1)) * plot_w
        parts.append(f'<text x="{x:.1f}" y="{height - 45}" text-anchor="middle" class="axis">{count}</text>')
    parts.append(f'<text x="{margin["left"] + plot_w / 2}" y="{height - 18}" text-anchor="middle" class="axis">row_count</text>')
    parts.append(f'<text x="20" y="{margin["top"] + plot_h / 2}" transform="rotate(-90 20 {margin["top"] + plot_h / 2})" text-anchor="middle" class="axis">elapsed_ms</text>')

    for series_index, stage in enumerate(stages):
        points = []
        for index, value in enumerate(data[stage]):
            x = margin["left"] + (index / max(1, len(counts) - 1)) * plot_w
            y = margin["top"] + plot_h - (value / y_max) * plot_h
            points.append((x, y, value))
        color = colors[series_index % len(colors)]
        point_text = " ".join(f"{x:.1f},{y:.1f}" for x, y, _ in points)
        parts.append(f'<polyline points="{point_text}" fill="none" stroke="{color}" stroke-width="2.5"/>')
        for x, y, value in points:
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>')
            parts.append(f'<text x="{x:.1f}" y="{y - 8:.1f}" text-anchor="middle" class="value">{value}</text>')
        legend_y = margin["top"] + series_index * 24
        parts.append(f'<rect x="{width - margin["right"] + 20}" y="{legend_y - 10}" width="14" height="14" fill="{color}" rx="2"/>')
        parts.append(f'<text x="{width - margin["right"] + 40}" y="{legend_y + 1}" class="label">{escape(stage.replace("perf.sample.", ""))}</text>')

    path.write_text(svg_wrap(width, height, "\n".join(parts)), encoding="utf-8")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    before = read_rows(BEFORE_PATH)
    after = read_rows(AFTER_PATH)

    labels = ["CRUD insert", "Delete+select", "CRUD total", "Sample insert", "Sample update"]
    before_values = [
        find(before, "stress.massive_data_crud.insert", 200) or 0,
        find(before, "stress.massive_data_crud.delete_then_select", 199) or 0,
        find(before, "stress.massive_data_crud", 200) or 0,
        find(before, "perf.sample.insert", 200) or 0,
        find(before, "perf.sample.update_one", 200) or 0,
    ]
    after_values = [
        find(after, "stress.massive_data_crud.insert", 200) or 0,
        find(after, "stress.massive_data_crud.delete_then_select", 199) or 0,
        find(after, "stress.massive_data_crud", 200) or 0,
        find(after, "perf.sample.insert", 200) or 0,
        find(after, "perf.sample.update_one", 200) or 0,
    ]
    bar_chart(
        OUT_DIR / "01_before_after_200_rows.svg",
        "200 行场景索引健康校验优化前后耗时对比",
        labels,
        [("before", before_values, "#9aa0a6"), ("after", after_values, "#1a73e8")],
    )

    line_chart(
        OUT_DIR / "02_perf_sample_trends_after_opt.svg",
        "多数据规模下核心操作耗时趋势",
        after,
        [
            "perf.sample.insert",
            "perf.sample.select_all",
            "perf.sample.update_one",
            "perf.sample.create_index",
            "perf.sample.indexed_select",
        ],
    )

    crud_labels = ["insert", "select_all", "update+select", "delete+select"]
    crud_values = [
        find(after, "stress.massive_data_crud.insert", 200) or 0,
        find(after, "stress.massive_data_crud.select_all", 200) or 0,
        find(after, "stress.massive_data_crud.update_then_select", 1) or 0,
        find(after, "stress.massive_data_crud.delete_then_select", 199) or 0,
    ]
    bar_chart(
        OUT_DIR / "03_massive_crud_components_after_opt.svg",
        "200 行大批量 CRUD 链路阶段耗时拆分",
        crud_labels,
        [("elapsed_ms", crud_values, "#188038")],
        width=860,
    )

    (OUT_DIR / "README.md").write_text(
        """# Performance Charts

| File | Purpose |
| --- | --- |
| `01_before_after_200_rows.svg` | Before/after comparison for 200-row insert/delete/update scenarios. |
| `02_perf_sample_trends_after_opt.svg` | Line chart for multi-scale CSV performance samples. |
| `03_massive_crud_components_after_opt.svg` | Breakdown of massive CRUD stages after optimization. |

Source CSV files:

- `../performance_samples.csv`
- `../performance_samples_after_opt.csv`
""",
        encoding="utf-8",
    )

    for path in sorted(OUT_DIR.glob("*.svg")):
        print(path)
    print(OUT_DIR / "README.md")


if __name__ == "__main__":
    main()
