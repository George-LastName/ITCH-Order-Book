// All SQL is POSTed to ClickHouse's HTTP interface, proxied through Vite at /ch.
// UInt64 columns (timestamp_ns) are returned as JS numbers rather than quoted
// strings via output_format_json_quote_64bit_integers=0. Values up to ~8.6e13
// (nanoseconds in a day) fit safely inside float64 precision (2^53 ≈ 9e15).

const CH_ENDPOINT = '/ch/?output_format_json_quote_64bit_integers=0'

async function query(sql) {
  const res = await fetch(CH_ENDPOINT, {
    method: 'POST',
    body: sql + '\nFORMAT JSON',
    headers: { 'Content-Type': 'text/plain' }
  })
  if (!res.ok) {
    const msg = await res.text()
    throw new Error(msg.trim())
  }
  const json = await res.json()
  return json.data  // array of row objects
}

// Returns [{name}] — only tables whose names end in _deltas.
export async function fetchTables() {
  return query(`
    SELECT name
    FROM system.tables
    WHERE database = 'Market_Data'
      AND name LIKE '%_deltas'
    ORDER BY name
  `)
}

// Returns [{stock_name}] for the given delta table, trimmed of padding spaces.
export async function fetchStocks(table) {
  return query(`
    SELECT DISTINCT trimRight(stock_name) AS stock_name
    FROM Market_Data.\`${table}\`
    WHERE stock_name != ''
    ORDER BY stock_name
  `)
}

// Returns {min_ts, max_ts} in nanoseconds from midnight for the given stock.
// stock_name is stored padded to 8 chars; we match with LIKE to handle padding.
export async function fetchTimeRange(table, stockName) {
  const rows = await query(`
    SELECT
      min(timestamp_ns) AS min_ts,
      max(timestamp_ns) AS max_ts
    FROM Market_Data.\`${table}\`
    WHERE trimRight(stock_name) = '${stockName}'
  `)
  return rows[0]
}

// Returns [{bucket, side, price, shares}] for the heatmap.
//
// bucket    — integer: intDiv(timestamp_ns, bucketNs). Multiply by bucketSec
//             to convert back to seconds-from-midnight for axis labels.
// side      — 'B' (bid) or 'S' (ask)
// price     — raw UInt32; divide by 10000 for dollars
// shares    — total depth at this price level at the end of the bucket
//
// argMax picks the final state of each price level within the bucket so that
// levels removed mid-bucket (shares = 0) are correctly handled; HAVING
// shares > 0 then drops removed levels from the result.
export async function fetchHeatmapData(table, stockName, startNs, endNs, bucketNs) {
  return query(`
    SELECT
      intDiv(timestamp_ns, ${bucketNs}) AS bucket,
      side,
      price,
      argMax(shares, timestamp_ns) AS shares
    FROM Market_Data.\`${table}\`
    WHERE trimRight(stock_name) = '${stockName}'
      AND timestamp_ns BETWEEN ${startNs} AND ${endNs}
    GROUP BY bucket, side, price
    HAVING shares > 0
    ORDER BY bucket, side, price
  `)
}
