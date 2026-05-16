import csv
from html import escape
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CSV_PATH = ROOT / "build" / "performance_samples.csv"
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


def metric_value(rows, test_id, stage, row_count, variant="", metric="elapsed_ms"):
    matches = [
        row
        for row in rows
        if row.get("test_id") == test_id
        and row.get("stage") == stage
        and row.get("row_count") == str(row_count)
        and row.get("variant", "") == variant
        and row.get("metric") == metric
    ]
    return to_int(matches[-1].get("value")) if matches else 0


def svg_wrap(width, height, body):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<style>
text {{ font-family: Arial, sans-serif; fill: #202124; }}
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


def bar_chart(path, title, labels, series, width=980, height=560, y_label="ms"):
    margin = {"left": 78, "right": 42, "top": 70, "bottom": 118}
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

    group_w = plot_w / max(1, len(labels))
    bar_gap = 8
    bar_w = min(34, max(6, (group_w - 24) / max(1, len(series)) - bar_gap))
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
        legend_x += max(140, len(name) * 8 + 42)

    path.write_text(svg_wrap(width, height, "\n".join(parts)), encoding="utf-8")


def line_chart(path, title, counts, series, width=980, height=560, y_label="ms"):
    margin = {"left": 78, "right": 165, "top": 70, "bottom": 80}
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
    for index, count in enumerate(counts):
        x = margin["left"] + (index / max(1, len(counts) - 1)) * plot_w
        parts.append(f'<text x="{x:.1f}" y="{height - 45}" text-anchor="middle" class="axis">{count}</text>')
    parts.append(f'<text x="{margin["left"] + plot_w / 2}" y="{height - 18}" text-anchor="middle" class="axis">row_count</text>')
    parts.append(f'<text x="20" y="{margin["top"] + plot_h / 2}" transform="rotate(-90 20 {margin["top"] + plot_h / 2})" text-anchor="middle" class="axis">{escape(y_label)}</text>')

    for series_index, (name, values, color) in enumerate(series):
        points = []
        for index, value in enumerate(values):
            x = margin["left"] + (index / max(1, len(counts) - 1)) * plot_w
            y = margin["top"] + plot_h - (value / y_max) * plot_h
            points.append((x, y, value))
        parts.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y, _ in points)}" fill="none" stroke="{color}" stroke-width="2.5"/>')
        for x, y, value in points:
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>')
            parts.append(f'<text x="{x:.1f}" y="{y - 8:.1f}" text-anchor="middle" class="value">{value}</text>')
        legend_y = margin["top"] + series_index * 24
        parts.append(f'<rect x="{width - margin["right"] + 20}" y="{legend_y - 10}" width="14" height="14" fill="{color}" rx="2"/>')
        parts.append(f'<text x="{width - margin["right"] + 40}" y="{legend_y + 1}" class="label">{escape(name)}</text>')

    path.write_text(svg_wrap(width, height, "\n".join(parts)), encoding="utf-8")


def values(rows, test_id, stage, counts, variant="", metric="elapsed_ms"):
    return [metric_value(rows, test_id, stage, count, variant, metric) for count in counts]


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rows = read_rows(CSV_PATH)
    counts = [50, 100, 200, 500]
    index_order_counts = [50, 100, 200, 500, 1000, 5000]
    colors = ["#1a73e8", "#188038", "#f29900", "#d93025", "#9334e6", "#00897b"]

    line_chart(
        OUT_DIR / "04_02_index_order_by_impact.svg",
        "Index Impact on ORDER BY",
        index_order_counts,
        [
            ("ASC without index", values(rows, "index_order_by_impact", "index_order_by_impact.order_by_asc", index_order_counts, "without_secondary_index"), colors[0]),
            ("ASC with index", values(rows, "index_order_by_impact", "index_order_by_impact.order_by_asc", index_order_counts, "with_secondary_index"), colors[1]),
            ("DESC without index", values(rows, "index_order_by_impact", "index_order_by_impact.order_by_desc", index_order_counts, "without_secondary_index"), colors[3]),
            ("DESC with index", values(rows, "index_order_by_impact", "index_order_by_impact.order_by_desc", index_order_counts, "with_secondary_index"), colors[4]),
        ],
    )

    line_chart(
        OUT_DIR / "04_03_concurrent_crud.svg",
        "Sequential vs Four-Client CRUD",
        counts,
        [
            ("single total", values(rows, "concurrent_crud", "concurrent_crud.total", counts, "single_client_sequential"), colors[0]),
            ("parallel total", values(rows, "concurrent_crud", "concurrent_crud.total", counts, "four_client_parallel"), colors[3]),
        ],
    )

    massive_crud_stages = [
        ("insert", "massive_crud.insert", colors[0]),
        ("select_all", "massive_crud.select_all", colors[1]),
        ("update_one", "massive_crud.update_one", colors[2]),
        ("delete_one", "massive_crud.delete_one", colors[3]),
        ("select_after_delete", "massive_crud.select_after_delete", colors[4]),
    ]
    line_chart(
        OUT_DIR / "04_04_massive_crud_breakdown.svg",
        "Massive CRUD Stage Time",
        counts,
        [(name, values(rows, "massive_crud", stage, counts, "row_count"), color) for name, stage, color in massive_crud_stages],
    )

    line_chart(
        OUT_DIR / "04_05_index_build_cost.svg",
        "Index Build Cost",
        counts,
        [("create_index", values(rows, "index_build", "index_build.create_index", counts, "row_count"), colors[0])],
    )

    line_chart(
        OUT_DIR / "04_06_index_repair_cost.svg",
        "Index Runtime Repair Cost",
        counts,
        [
            ("healthy index file", values(rows, "index_repair", "index_repair.update", counts, "healthy_index_file"), colors[1]),
            ("deleted index file", values(rows, "index_repair", "index_repair.update", counts, "deleted_index_file"), colors[3]),
        ],
    )

    line_chart(
        OUT_DIR / "04_07_foreign_key_cascade.svg",
        "Foreign Key Cascade Cost",
        counts,
        [
            ("cascade_update", values(rows, "foreign_key_cascade", "foreign_key_cascade.cascade_update", counts, "row_count"), colors[0]),
            ("cascade_delete", values(rows, "foreign_key_cascade", "foreign_key_cascade.cascade_delete", counts, "row_count"), colors[3]),
        ],
    )

    (OUT_DIR / "README.md").write_text(
        """# Performance Charts

| File | Purpose |
| --- | --- |
| `04_02_index_order_by_impact.svg` | Indexed vs non-indexed ORDER BY comparison from 50 to 5000 rows. |
| `04_03_concurrent_crud.svg` | Sequential vs four-client CRUD totals. |
| `04_04_massive_crud_breakdown.svg` | CRUD stage trends across row counts. |
| `04_05_index_build_cost.svg` | Index build cost across row counts. |
| `04_06_index_repair_cost.svg` | Healthy index update vs repair-after-ablation cost. |
| `04_07_foreign_key_cascade.svg` | Cascade update/delete cost across row counts. |

Source CSV:

- `../performance_samples.csv`
""",
        encoding="utf-8",
    )

    for path in sorted(OUT_DIR.glob("*.svg")):
        print(path)
    print(OUT_DIR / "README.md")


if __name__ == "__main__":
    main()
