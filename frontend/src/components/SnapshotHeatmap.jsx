import { useState, useEffect, useMemo } from 'react'
import ReactECharts from 'echarts-for-react'
import { fetchSnapshotData, fetchTimeRange } from '../api/clickhouse.js'

function fmtTimeSec(sec) {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`
}

function fmtPrice(raw) {
  return `$${(raw / 10000).toFixed(2)}`
}

// Minimum bucket is 1 min — snapshots are taken every 60 s so finer
// resolution would just repeat the same value.
const BUCKET_OPTIONS = [
  { label: '1 min',  sec: 60   },
  { label: '5 min',  sec: 300  },
  { label: '15 min', sec: 900  },
  { label: '1 hr',   sec: 3600 },
]

const ctrlStyle = {
  background: '#1e2130', color: '#e0e0e0',
  border: '1px solid #444', borderRadius: 4,
  padding: '4px 8px', fontSize: 13, cursor: 'pointer',
}

export default function SnapshotHeatmap({ snapshotTable, stock }) {
  const [bucketSec, setBucketSec] = useState(60)
  const [timeRange, setTimeRange] = useState(null)
  const [data, setData]           = useState([])
  const [loading, setLoading]     = useState(false)
  const [error, setError]         = useState(null)

  // The snapshots table shares the same time range as the deltas table for
  // the same stock, so we query time range from the snapshot table directly.
  useEffect(() => {
    if (!snapshotTable || !stock) return
    setTimeRange(null)
    setData([])
    setError(null)
    fetchTimeRange(snapshotTable, stock)
      .then(setTimeRange)
      .catch(e => setError(e.message))
  }, [snapshotTable, stock])

  useEffect(() => {
    if (!snapshotTable || !stock || !timeRange) return
    setLoading(true)
    setError(null)
    const bucketNs = bucketSec * 1_000_000_000
    fetchSnapshotData(snapshotTable, stock, timeRange.min_ts, timeRange.max_ts, bucketNs)
      .then(rows => { setData(rows); setLoading(false) })
      .catch(e   => { setError(e.message); setLoading(false) })
  }, [snapshotTable, stock, timeRange, bucketSec])

  const option = useMemo(() => {
    if (data.length === 0) return {}

    const dataBuckets = [...new Set(data.map(r => +r.bucket))].sort((a, b) => a - b)
    const prices      = [...new Set(data.map(r => +r.price)) ].sort((a, b) => a - b)

    const minBucket = dataBuckets[0]
    const maxBucket = dataBuckets[dataBuckets.length - 1]
    const buckets = []
    for (let b = minBucket; b <= maxBucket; b++) buckets.push(b)

    const bucketToIdx = new Map(buckets.map((b, i) => [b, i]))
    const priceToIdx  = new Map(prices.map( (p, i) => [p, i]))

    const xLabels = buckets.map(b => fmtTimeSec(b * bucketSec))
    const yLabels = prices.map(fmtPrice)

    const bidData = []
    const askData = []
    let maxLog = 0
    let minLog = Infinity

    for (const r of data) {
      const xi     = bucketToIdx.get(+r.bucket)
      const yi     = priceToIdx.get(+r.price)
      const raw    = +r.shares
      const logVal = Math.log1p(raw)
      if (logVal > maxLog) maxLog = logVal
      if (logVal < minLog) minLog = logVal

      if (r.side === 'B') bidData.push([xi, yi, logVal])
      else                askData.push([xi, yi, logVal])
    }

    function tooltipFmt(params) {
      const [xi, yi, logVal] = params.value
      const rawShares = Math.round(Math.expm1(logVal))
      return [
        `<b>${params.seriesName}</b>`,
        `Time:  ${xLabels[xi]}`,
        `Price: ${yLabels[yi]}`,
        `Depth: ${rawShares.toLocaleString()} shares`,
      ].join('<br/>')
    }

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
        right: 20, top: 10,
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
          color: '#aaa', fontSize: 11,
          interval: (i) => dollarSet.has(prices[i]),
        },
        axisLine:  { lineStyle: { color: '#444' } },
        splitLine: { show: false },
      },

      dataZoom: [
        { type: 'slider', xAxisIndex: 0, start: 0, end: 100, bottom: 60,
          textStyle: { color: '#aaa' }, borderColor: '#444', fillerColor: 'rgba(45,53,85,0.5)' },
        { type: 'slider', yAxisIndex: 0, start: 0, end: 100, right: 155,
          textStyle: { color: '#aaa' }, borderColor: '#444', fillerColor: 'rgba(45,53,85,0.5)' },
        { type: 'inside', xAxisIndex: 0 },
        { type: 'inside', yAxisIndex: 0 },
      ],

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
          progressive: 2000,
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
