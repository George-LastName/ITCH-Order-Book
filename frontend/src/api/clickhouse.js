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
  return json.data
}

export async function fetchTables() {
  return query(`
    SELECT name
    FROM system.tables
    WHERE database = 'Market_Data'
      AND name LIKE '%_deltas'
    ORDER BY name
  `)
}

export async function fetchStocks(table) {
  return query(`
    SELECT DISTINCT trimRight(stock_name) AS stock_name
    FROM Market_Data.\`${table}\`
    WHERE stock_name != ''
    ORDER BY stock_name
  `)
}

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

// Returns [{bucket, side, price, shares}].
// bucket = intDiv(timestamp_ns, bucketNs) — multiply by bucketSec for seconds.
// price  = raw UInt32 (divide by 10000 for USD).
// shares = final depth at this price level within the bucket (0-removed levels excluded).
// Snapshot data: ARRAY JOIN expands the bid/ask price+shares arrays into
// individual rows, then groups into time buckets the same way as deltas.
// Snapshots are taken every 60 s so bucketNs should be >= 60_000_000_000.
// No TOP_N filter needed — snapshots already store only the top 10 levels.
export async function fetchSnapshotData(snapshotTable, stockName, startNs, endNs, bucketNs) {
  return query(`
    SELECT bucket, side, price, shares FROM (
      SELECT
        intDiv(timestamp_ns, ${bucketNs}) AS bucket,
        'B'  AS side,
        price,
        argMax(shares, timestamp_ns) AS shares
      FROM Market_Data.\`${snapshotTable}\`
      ARRAY JOIN bid_prices AS price, bid_shares AS shares
      WHERE trimRight(stock_name) = '${stockName}'
        AND timestamp_ns BETWEEN ${startNs} AND ${endNs}
      GROUP BY bucket, side, price
      HAVING shares > 0

      UNION ALL

      SELECT
        intDiv(timestamp_ns, ${bucketNs}) AS bucket,
        'S'  AS side,
        price,
        argMax(shares, timestamp_ns) AS shares
      FROM Market_Data.\`${snapshotTable}\`
      ARRAY JOIN ask_prices AS price, ask_shares AS shares
      WHERE trimRight(stock_name) = '${stockName}'
        AND timestamp_ns BETWEEN ${startNs} AND ${endNs}
      GROUP BY bucket, side, price
      HAVING shares > 0
    )
    ORDER BY bucket, side, price
  `)
}

export async function fetchHeatmapData(table, stockName, startNs, endNs, bucketNs) {
  // Inner query: aggregate to final state of each price level per bucket.
  // Window function then ranks levels within each (bucket, side):
  //   bids  → ranked by price DESC (highest price = best bid = rank 1)
  //   asks  → ranked by price ASC  (lowest  price = best ask = rank 1)
  // Outer query keeps only the top 10 levels per side per bucket, matching
  // the TOP_N cap used by the C++ snapshots.
  return query(`
    SELECT bucket, side, price, shares
    FROM (
      SELECT
        intDiv(timestamp_ns, ${bucketNs}) AS bucket,
        side,
        price,
        argMax(shares, timestamp_ns) AS shares,
        row_number() OVER (
          PARTITION BY bucket, side
          ORDER BY if(side = 'B', 0 - toInt64(price), toInt64(price))
        ) AS rnk
      FROM Market_Data.\`${table}\`
      WHERE trimRight(stock_name) = '${stockName}'
        AND timestamp_ns BETWEEN ${startNs} AND ${endNs}
      GROUP BY bucket, side, price
      HAVING shares > 0
    )
    WHERE rnk <= 10
    ORDER BY bucket, side, price
  `)
}
