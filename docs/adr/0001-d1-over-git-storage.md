# ADR 0001: Use Cloudflare D1 Over Direct Git-Based Storage for Telemetry

## Status
Accepted

## Date
2026-11-13

## Context
In earlier prototypes such as `kreier/T18`, device metrics and network logs were explored for direct commit and push to Git repositories (e.g. GitHub repositories using personal access tokens or GitHub API commits). While committing data to Git provides an immutable audit trail and free hosting on GitHub Pages, scaling this approach to a continuous 10-minute cadence across multiple deployed field stations introduces severe architectural bottlenecks:

1. **Write Contention & Merge Conflicts**: Multiple autonomous ESP32 nodes attempting to push or commit to the same repository branch simultaneously result in frequent Git rebase/merge conflicts.
2. **Repository Bloat**: Committing raw scan surveys and network latency metrics every 10 minutes generates 144 commits per station per day. For 10 stations, that is over 500,000 commits and hundreds of megabytes of Git object packfile churn per year, degrading clone performance and exceeding GitHub soft storage limits.
3. **API Rate Limiting**: The GitHub REST and GraphQL APIs enforce strict rate limits (typically 5,000 requests/hour per authenticated token), which can be exhausted during network catch-up spikes when offline nodes flush spooled data.
4. **Microcontroller Resource Limits**: Running an HTTPS Git client or heavy REST payloads with TLS negotiation and JSON tree construction on an ESP32 (4 MB Flash, 520 KB SRAM) consumes substantial heap memory and execution time, draining power and increasing crash risk.
5. **Slow Querying**: Git is a content-addressable file store, not a time-series database. Querying historical latency across date ranges or aggregating percentiles for dashboard visualization requires cloning or parsing large JSON/CSV trees in browser clients.

## Decision
We choose **Cloudflare D1** (distributed SQLite running on Cloudflare's serverless edge infrastructure) as the primary ingestion and analytical database for `kreier/wifi`, accessed via a lightweight Cloudflare Worker API.

Key elements of this decision:
- **Low-Latency Edge Ingestion**: ESP32 nodes upload telemetry via lightweight `POST /data` requests terminating at the closest Cloudflare edge location.
- **Relational SQL Queries**: D1 allows indexed SQL queries for fast station status lookups, latest telemetry retrieval, and historical time-series slicing.
- **Two-Tier Retention Policy**:
  - Raw 10-minute measurements and scan surveys are retained for 30 days.
  - A scheduled daily Cron Trigger (`00:15 UTC`) aggregates the previous day's metrics into a compact `daily_summaries` table (min, avg, max latencies and sample counts per network) preserved indefinitely.
  - Raw records older than 30 days are automatically purged during the daily cron run.
- **Decoupled Permanent Git Archiving**: For permanent public archives or repository exports, the reserved `data/` directory can receive scheduled batch exports (e.g., monthly summary dumps or compressed SQLite snapshots via GitHub Actions), keeping the operational database decoupled from Git version control.

## Consequences

### Positive
- Negligible latency and power consumption on the ESP32 during telemetry upload.
- Zero merge conflicts or write locks between distributed field stations.
- Interactive, responsive web dashboard with sub-50ms SQL queries.
- Predictable storage footprint via automated 30-day raw data pruning and daily aggregation.
- Eliminates GitHub API token exposure on field-deployed hardware.

### Negative
- Requires maintaining a Cloudflare account and D1 database binding.
- D1 rows and operations are subject to Cloudflare free/paid tier limits (though Cloudflare's free tier of 5M read queries/day and 100k write queries/day is more than sufficient for fleet monitoring).
