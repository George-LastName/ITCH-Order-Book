import { useState, useEffect, useMemo } from 'react'
import ReactECharts from 'echarts-for-react'
import { fetchHeatmapData, fetchTimeRange } from '../api/clickhouse.js'

// Seconds from midnight → "HH:MM"
function fmtTimeSec(sec) {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`
}

// Seconds from midnight → "HH:MM:SS"
function fmtTimeSecFull(sec) {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  const s = sec % 60
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
}

// Raw UInt32 price (4 implied decimal places) → "$NNN.NN"
function fmtPrice(raw) {
  return `$${(raw / 10000).toFixed(2)}`
}

const BUCKET_OPTIONS = [
  { label: '1 s',   sec: 1   },
  { label: '10 s',  sec: 10  },
  { label: '30 s',  sec: 30  },
  { label: '1 min', sec: 60  },
  { label: '5 min', sec: 300 },
]

const ctrlStyle = {
  background: '#1e2130', color: '#e0e0e0',
  border: '1px solid #444', borderRadius: 4,
  padding: '4px 8px', fontSize: 13, cursor: 'pointer',
}

export default function OrderBookHeatmap({ table, stock }) {
  const [bucketSec, setBucketSec] = useState(60)
  const [timeRange, setTimeRange] = useState(null)
  const [data, setData]           = useState([])
  const [loading, setLoading]     = useState(false)
  const [error, setError]         = useState(null)

  // Reset and fetch time range when stock changes
  useEffect(() => {
    if (!table || !stock) return
    setTimeRange(null)
    setData([])
    setError(null)
    fetchTimeRange(table, stock)
      .then(setTimeRange)
      .catch(e => setError(e.message))
  }, [table, stock])

  // Fetch all data for the stock's full time range when timeRange or bucketSec changes.
  // The dataZoom sliders handle interactive narrowing — no need to re-query on pan/zoom.
  useEffect(() => {
    if (!table || !stock || !timeRange) return
    setLoading(true)
    setError(null)
    const bucketNs = bucketSec * 1_000_000_000
    fetchHeatmapData(table, stock, timeRange.min_ts, timeRange.max_ts, bucketNs)
      .then(rows => { setData(rows); setLoading(false) })
      .catch(e   => { setError(e.message); setLoading(false) })
  }, [table, stock, timeRange, bucketSec])

  // Build the full ECharts option object from the fetched data.
  const option = useMemo(() => {
    if (data.length === 0) return {}

    // ── Build category arrays ─────────────────────────────────────────────
    const dataBuckets = [...new Set(data.map(r => +r.bucket))].sort((a, b) => a - b)
    const prices      = [...new Set(data.map(r => +r.price)) ].sort((a, b) => a - b)

    // Fill the full contiguous range min→max so sparse pre-market periods
    // occupy the same pixel width per bucket as dense market-hours periods.
    // Empty buckets simply render blank — no data cell is emitted for them.
    const minBucket = dataBuckets[0]
    const maxBucket = dataBuckets[dataBuckets.length - 1]
    const buckets = []
    for (let b = minBucket; b <= maxBucket; b++) buckets.push(b)

    // O(1) index lookup — avoids indexOf() over thousands of categories
    const bucketToIdx = new Map(buckets.map((b, i) => [b, i]))
    const priceToIdx  = new Map(prices.map( (p, i) => [p, i]))

    const xLabels = buckets.map(b => fmtTimeSec(b * bucketSec))
    const yLabels = prices.map(fmtPrice)

    // ── Encode data as [xIdx, yIdx, log1p(shares), rawShares] ────────────
    // log1p keeps faint levels visible next to deep ones.
    // rawShares is stored as a 4th element so the tooltip can show actual depth.
    const bidData = []
    const askData = []
    let maxLog = 0
    let minLog = Infinity

    for (const r of data) {
      const xi      = bucketToIdx.get(+r.bucket)
      const yi      = priceToIdx.get(+r.price)
      const raw     = +r.shares
      const logVal  = Math.log1p(raw)
      if (logVal > maxLog) maxLog = logVal
      if (logVal < minLog) minLog = logVal

      if (r.side === 'B') bidData.push([xi, yi, logVal])
      else                askData.push([xi, yi, logVal])
    }

    // ── Tooltip formatter ─────────────────────────────────────────────────
    function tooltipFmt(params) {
      const [xi, yi, logVal] = params.value
      const rawShares = Math.round(Math.expm1(logVal))
      return [
        `<b>${params.seriesName}</b>`,
        `Time:  ${bucketSec < 60 ? fmtTimeSecFull(buckets[xi] * bucketSec) : xLabels[xi]}`,
        `Price: ${yLabels[yi]}`,
        `Depth: ${rawShares.toLocaleString()} shares`,
      ].join('<br/>')
    }

    // Y-axis: label only prices that fall on a round dollar boundary.
    const dollarSet = new Set(prices.filter(p => p % 10000 === 0))

    const xStepSec = 900  // 15-minute label intervals

    return {
      backgroundColor: '#0f1117',

      tooltip: {
        trigger: 'item',
        formatter: tooltipFmt,
        backgroundColor: '#1e2130',
        borderColor: '#444',
        textStyle: { color: '#e0e0e0', fontSize: 12 },
      },

      toolbox: {
        right: 20,
        top: 10,
        iconStyle: { borderColor: '#aaa' },
        feature: {
          dataZoom: { title: { zoom: 'Box zoom', back: 'Reset zoom' } },
          restore:  { title: 'Reset' },
          saveAsImage: { title: 'Save PNG', backgroundColor: '#0f1117' },
        },
      },

      grid: { top: 50, right: 210, bottom: 120, left: 100 },

      xAxis: {
        type: 'category',
        data: xLabels,
        name: 'Time (HH:MM)',
        nameLocation: 'middle',
        nameGap: 75,
        nameTextStyle: { color: '#888', fontSize: 12 },
        axisLabel: {
          color: '#aaa', rotate: 30, fontSize: 11,
          formatter: v => v,
          interval: (i) => (buckets[i] * bucketSec) % xStepSec === 0,
        },
        axisLine:  { lineStyle: { color: '#444' } },
        splitLine: { show: false },
      },

      yAxis: {
        type: 'category',
        data: yLabels,
        name: 'Price (USD)',
        nameLocation: 'middle',
        nameGap: 80,
        nameTextStyle: { color: '#888', fontSize: 12 },
        axisLabel: {
          color: '#aaa',
          fontSize: 11,
          interval: (i) => dollarSet.has(prices[i]),
        },
        axisLine:  { lineStyle: { color: '#444' } },
        splitLine: { show: false },
      },

      // dataZoom gives interactive drag-sliders + scroll-wheel zoom on both axes.
      // The Y slider starts at P2–P98 so outliers are clipped by default.
      dataZoom: [
        { type: 'slider', xAxisIndex: 0, start: 0,      end: 100,  bottom: 60,
          textStyle: { color: '#aaa' }, borderColor: '#444', fillerColor: 'rgba(45,53,85,0.5)' },
        { type: 'slider', yAxisIndex: 0, start: 0, end: 100, right: 155,
          textStyle: { color: '#aaa' }, borderColor: '#444', fillerColor: 'rgba(45,53,85,0.5)' },
        { type: 'inside', xAxisIndex: 0 },
        { type: 'inside', yAxisIndex: 0 },
      ],

      // Two visual maps — one per series — give independent colour-range handles
      // for bid depth and ask depth.
      visualMap: [
        {
          seriesIndex: 0,
          min: minLog, max: maxLog,
          calculable: true,
          orient: 'vertical',
          right: 10, top: 50,
          inRange: { color: ['#00ff88', '#00ddaa', '#00aadd', '#0066ff', '#0000ff'] },
          textStyle: { color: '#aaa', fontSize: 11 },
          text: ['Bid High', 'Low'],
          formatter: v => Math.round(Math.expm1(v)).toLocaleString(),
        },
        {
          seriesIndex: 1,
          min: minLog, max: maxLog,
          calculable: true,
          orient: 'vertical',
          right: 10, top: 280,
          inRange: { color: ['#ffee00', '#ffbb00', '#ff7700', '#ff2200', '#ff0000'] },
          textStyle: { color: '#aaa', fontSize: 11 },
          text: ['Ask High', 'Low'],
          formatter: v => Math.round(Math.expm1(v)).toLocaleString(),
        },
      ],

      series: [
        {
          name: 'Bid',
          type: 'heatmap',
          data: bidData,
          emphasis: { itemStyle: { borderColor: '#fff', borderWidth: 1 } },
          progressive: 2000,          // render in chunks to keep UI responsive
          progressiveThreshold: 5000,
        },
        {
          name: 'Ask',
          type: 'heatmap',
          data: askData,
          emphasis: { itemStyle: { borderColor: '#fff', borderWidth: 1 } },
          progressive: 2000,
          progressiveThreshold: 5000,
        },
      ],
    }
  }, [data, bucketSec])

  return (
    <div>
      {/* Controls */}
      <div style={{
        display: 'flex', gap: '1rem', alignItems: 'center',
        marginBottom: 12, padding: '8px 12px',
        background: '#13161f', borderRadius: 6, border: '1px solid #2a2d3a',
      }}>
        <label style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ fontSize: 13, color: '#aaa' }}>Time bucket</span>
          <select style={ctrlStyle} value={bucketSec} onChange={e => setBucketSec(+e.target.value)}>
            {BUCKET_OPTIONS.map(o => (
              <option key={o.sec} value={o.sec}>{o.label}</option>
            ))}
          </select>
        </label>

        {loading && <span style={{ fontSize: 13, color: '#888' }}>Loading…</span>}
        {error   && <span style={{ fontSize: 13, color: '#f87171' }}>Error: {error}</span>}
        {!loading && !error && data.length > 0 && (
          <span style={{ fontSize: 12, color: '#555' }}>
            {data.length.toLocaleString()} cells — drag sliders or scroll to zoom
          </span>
        )}
      </div>

      <ReactECharts
        option={option}
        style={{ height: 640, width: '100%' }}
        opts={{ renderer: 'canvas' }}
        showLoading={loading}
        loadingOption={{ text: 'Loading…', color: '#6baed6', textColor: '#aaa', maskColor: '#0f1117' }}
        notMerge={true}
      />
    </div>
  )
}
