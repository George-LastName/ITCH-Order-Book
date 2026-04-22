import { useEffect, useRef, useState, useMemo } from 'react'
import * as d3 from 'd3'
import { fetchHeatmapData, fetchTimeRange } from '../api/clickhouse.js'

const MARGIN = { top: 20, right: 120, bottom: 50, left: 90 }
const WIDTH  = 1160
const HEIGHT = 560

// ns from midnight → "HH:MM:SS" for <input type="time">
function nsToTimeStr(ns) {
  const s   = Math.floor(ns / 1_000_000_000)
  const h   = Math.floor(s / 3600)
  const m   = Math.floor((s % 3600) / 60)
  const sec = s % 60
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(sec).padStart(2,'0')}`
}

// "HH:MM" or "HH:MM:SS" → ns from midnight
function timeStrToNs(str) {
  const [h = 0, m = 0, s = 0] = str.split(':').map(Number)
  return (h * 3600 + m * 60 + s) * 1_000_000_000
}

// Raw UInt32 price (4 implied decimal places) → "$NNN.NNNN"
function fmtPrice(raw) {
  return `$${(raw / 10000).toFixed(2)}`
}

// Seconds from midnight → "HH:MM"
function fmtTimeSec(sec) {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}`
}

const BUCKET_OPTIONS = [
  { label: '10 s',  sec: 10  },
  { label: '30 s',  sec: 30  },
  { label: '1 min', sec: 60  },
  { label: '5 min', sec: 300 },
]

const ctrlBase = {
  background: '#1e2130',
  color:      '#e0e0e0',
  border:     '1px solid #444',
  borderRadius: 4,
  padding:    '4px 8px',
  fontSize:   13,
}

const inputStyle  = { ...ctrlBase, width: 110, outline: 'none' }
const selectStyle = { ...ctrlBase, cursor: 'pointer' }
const btnStyle    = {
  ...ctrlBase,
  cursor:     'pointer',
  padding:    '4px 12px',
  background: '#2d3555',
  border:     '1px solid #4a5380',
}

const labelStyle  = { fontSize: 12, color: '#888' }
const sectionStyle = {
  display:       'flex',
  gap:           '1rem',
  alignItems:    'center',
  flexWrap:      'wrap',
  padding:       '8px 12px',
  background:    '#13161f',
  borderRadius:  6,
  border:        '1px solid #2a2d3a',
  marginBottom:  10,
}

export default function OrderBookHeatmap({ table, stock }) {
  const canvasRef = useRef(null)

  const [bucketSec, setBucketSec] = useState(60)

  // Bounds returned by the DB — the hard limits for the sliders
  const [timeRange, setTimeRange] = useState(null)

  // Applied fetch bounds (ns). Re-fetching fires when these change.
  const [tStart, setTStart] = useState(null)
  const [tEnd,   setTEnd]   = useState(null)

  // Draft strings in the time inputs (not yet applied)
  const [draftStart, setDraftStart] = useState('')
  const [draftEnd,   setDraftEnd]   = useState('')

  // Display price bounds in raw units (divide by 10000 for $).
  // null = not yet auto-computed from data.
  const [priceMinRaw, setPriceMinRaw] = useState(null)
  const [priceMaxRaw, setPriceMaxRaw] = useState(null)

  const [data,    setData]    = useState([])
  const [loading, setLoading] = useState(false)
  const [error,   setError]   = useState(null)

  const innerW = WIDTH  - MARGIN.left - MARGIN.right
  const innerH = HEIGHT - MARGIN.top  - MARGIN.bottom

  // ── Reset everything when stock changes ───────────────────────────────────
  useEffect(() => {
    if (!table || !stock) return
    setTimeRange(null)
    setTStart(null)
    setTEnd(null)
    setDraftStart('')
    setDraftEnd('')
    setPriceMinRaw(null)
    setPriceMaxRaw(null)
    setData([])
    setError(null)

    fetchTimeRange(table, stock)
      .then(range => {
        setTimeRange(range)
        setTStart(range.min_ts)
        setTEnd(range.max_ts)
        setDraftStart(nsToTimeStr(range.min_ts))
        setDraftEnd(nsToTimeStr(range.max_ts))
      })
      .catch(e => setError(e.message))
  }, [table, stock])

  // ── Fetch heatmap data whenever applied time range or bucket changes ───────
  useEffect(() => {
    if (!table || !stock || tStart === null || tEnd === null) return
    setLoading(true)
    setError(null)
    const bucketNs = bucketSec * 1_000_000_000
    fetchHeatmapData(table, stock, tStart, tEnd, bucketNs)
      .then(rows => { setData(rows); setLoading(false) })
      .catch(e   => { setError(e.message); setLoading(false) })
  }, [table, stock, tStart, tEnd, bucketSec])

  // ── Auto-set price range on first data load for this stock ────────────────
  // Uses the 2nd–98th percentile of distinct price levels so outliers that are
  // orders of magnitude away from the main cluster don't collapse the scale.
  useEffect(() => {
    if (data.length === 0 || priceMinRaw !== null) return
    const prices = [...new Set(data.map(r => +r.price))].sort((a, b) => a - b)
    const p2  = prices[Math.floor(prices.length * 0.02)]  ?? prices[0]
    const p98 = prices[Math.floor(prices.length * 0.98)]  ?? prices[prices.length - 1]
    setPriceMinRaw(p2)
    setPriceMaxRaw(p98)
  }, [data, priceMinRaw])

  // ── Compute scales + filter data to visible price window ──────────────────
  // Price range changes only re-run this memo + re-draw; no network request.
  const { scales, visibleData } = useMemo(() => {
    const pMin = priceMinRaw
    const pMax = priceMaxRaw

    const visible = (pMin !== null && pMax !== null)
      ? data.filter(r => +r.price >= pMin && +r.price <= pMax)
      : data

    if (visible.length === 0) return { scales: null, visibleData: [] }

    const buckets = [...new Set(visible.map(r => +r.bucket))].sort((a, b) => a - b)
    const prices  = [...new Set(visible.map(r => +r.price)) ].sort((a, b) => a - b)

    const xScale = d3.scaleLinear([buckets[0], buckets[buckets.length - 1]], [0, innerW])
    const yScale = d3.scaleLinear([prices[0],  prices[prices.length - 1]],   [innerH, 0])

    const maxShares = d3.max(visible, r => +r.shares)
    const bidColor  = d3.scaleSequential([0, Math.log1p(maxShares)], d3.interpolateBlues)
    const askColor  = d3.scaleSequential([0, Math.log1p(maxShares)], d3.interpolateReds)

    // Cell width: pixels per bucket
    const cellW = buckets.length > 1
      ? xScale(buckets[1]) - xScale(buckets[0])
      : Math.max(innerW / 100, 2)

    // Cell height: median gap between adjacent price levels
    const steps = []
    for (let i = 1; i < prices.length; i++) steps.push(prices[i] - prices[i - 1])
    const medianStep = steps.length > 0
      ? steps.sort((a, b) => a - b)[Math.floor(steps.length / 2)]
      : 1
    const cellH = Math.abs(yScale(0) - yScale(medianStep))

    const xTicks = xScale.ticks(10).map(t => ({
      x:     xScale(t),
      label: fmtTimeSec(t * bucketSec),
    }))
    const yTicks = yScale.ticks(10).map(t => ({
      y:     yScale(t),
      label: fmtPrice(t),
    }))

    return {
      scales: { xScale, yScale, bidColor, askColor, cellW, cellH, xTicks, yTicks },
      visibleData: visible,
    }
  }, [data, priceMinRaw, priceMaxRaw, innerW, innerH, bucketSec])

  // ── Draw heatmap cells onto canvas ────────────────────────────────────────
  useEffect(() => {
    if (!canvasRef.current || !scales || visibleData.length === 0) return

    const { xScale, yScale, bidColor, askColor, cellW, cellH } = scales
    const canvas = canvasRef.current
    const dpr    = window.devicePixelRatio || 1

    canvas.width        = innerW * dpr
    canvas.height       = innerH * dpr
    canvas.style.width  = `${innerW}px`
    canvas.style.height = `${innerH}px`

    const ctx = canvas.getContext('2d')
    ctx.scale(dpr, dpr)
    ctx.clearRect(0, 0, innerW, innerH)

    for (const row of visibleData) {
      const shares = +row.shares
      if (shares === 0) continue
      const x         = xScale(+row.bucket)
      const y         = yScale(+row.price)
      const intensity = Math.log1p(shares)
      ctx.fillStyle   = row.side === 'B' ? bidColor(intensity) : askColor(intensity)
      ctx.fillRect(x, y - cellH, cellW + 1, cellH + 1) // +1 prevents hairline gaps
    }
  }, [visibleData, scales, innerW, innerH])

  // ── Helpers ───────────────────────────────────────────────────────────────
  function applyTimeRange() {
    const s = timeStrToNs(draftStart)
    const e = timeStrToNs(draftEnd)
    if (s >= e) return  // silently ignore inverted range
    setTStart(s)
    setTEnd(e)
  }

  function resetTimeRange() {
    if (!timeRange) return
    setDraftStart(nsToTimeStr(timeRange.min_ts))
    setDraftEnd(nsToTimeStr(timeRange.max_ts))
    setTStart(timeRange.min_ts)
    setTEnd(timeRange.max_ts)
  }

  // Dollar string → raw, applying to the price range immediately
  function handlePriceMin(val) {
    const raw = Math.round(parseFloat(val) * 10000)
    if (!isNaN(raw)) setPriceMinRaw(raw)
  }
  function handlePriceMax(val) {
    const raw = Math.round(parseFloat(val) * 10000)
    if (!isNaN(raw)) setPriceMaxRaw(raw)
  }

  const bidGradientId = 'bid-grad'
  const askGradientId = 'ask-grad'

  return (
    <div>

      {/* ── Controls ─────────────────────────────────────────────────────── */}
      <div style={{ display: 'flex', gap: 12, flexDirection: 'column', marginBottom: 16 }}>

        {/* Row 1: bucket + status */}
        <div style={sectionStyle}>
          <span style={labelStyle}>Time bucket</span>
          <select
            style={selectStyle}
            value={bucketSec}
            onChange={e => setBucketSec(+e.target.value)}
          >
            {BUCKET_OPTIONS.map(o => (
              <option key={o.sec} value={o.sec}>{o.label}</option>
            ))}
          </select>

          <span style={{ ...labelStyle, marginLeft: 8 }}>
            {loading && 'Loading…'}
            {error   && <span style={{ color: '#f87171' }}>Error: {error}</span>}
            {!loading && !error && visibleData.length > 0 &&
              `${visibleData.length.toLocaleString()} cells visible`}
          </span>
        </div>

        {/* Row 2: time range */}
        <div style={sectionStyle}>
          <span style={labelStyle}>Time range</span>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <input
              type="time"
              step="1"
              style={inputStyle}
              value={draftStart}
              onChange={e => setDraftStart(e.target.value)}
            />
            <span style={labelStyle}>to</span>
            <input
              type="time"
              step="1"
              style={inputStyle}
              value={draftEnd}
              onChange={e => setDraftEnd(e.target.value)}
            />
          </div>
          <button style={btnStyle} onClick={applyTimeRange}>Apply</button>
          <button style={{ ...btnStyle, background: '#1e2130', border: '1px solid #444' }} onClick={resetTimeRange}>
            Reset
          </button>
          {timeRange && (
            <span style={labelStyle}>
              Available: {nsToTimeStr(timeRange.min_ts).slice(0,5)}–{nsToTimeStr(timeRange.max_ts).slice(0,5)}
            </span>
          )}
        </div>

        {/* Row 3: price range */}
        <div style={sectionStyle}>
          <span style={labelStyle}>Price range ($)</span>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <input
              type="number"
              step="0.01"
              min="0"
              style={inputStyle}
              value={priceMinRaw !== null ? (priceMinRaw / 10000).toFixed(2) : ''}
              placeholder="Min"
              onChange={e => handlePriceMin(e.target.value)}
            />
            <span style={labelStyle}>to</span>
            <input
              type="number"
              step="0.01"
              min="0"
              style={inputStyle}
              value={priceMaxRaw !== null ? (priceMaxRaw / 10000).toFixed(2) : ''}
              placeholder="Max"
              onChange={e => handlePriceMax(e.target.value)}
            />
          </div>
          <button
            style={{ ...btnStyle, background: '#1e2130', border: '1px solid #444' }}
            onClick={() => setPriceMinRaw(null)}  // null → auto-recompute on next data
            title="Reset to auto P2–P98"
          >
            Auto
          </button>
          <span style={labelStyle}>Updates immediately (no refetch)</span>
        </div>

      </div>

      {/* ── Plot ─────────────────────────────────────────────────────────── */}
      <div style={{ position: 'relative', width: WIDTH, height: HEIGHT }}>

        <canvas
          ref={canvasRef}
          style={{ position: 'absolute', top: MARGIN.top, left: MARGIN.left, display: 'block' }}
        />

        <svg
          width={WIDTH}
          height={HEIGHT}
          style={{ position: 'absolute', top: 0, left: 0, pointerEvents: 'none' }}
        >
          <defs>
            <linearGradient id={bidGradientId} x1="0" x2="0" y1="1" y2="0">
              <stop offset="0%"   stopColor="#08306b" />
              <stop offset="100%" stopColor="#6baed6" />
            </linearGradient>
            <linearGradient id={askGradientId} x1="0" x2="0" y1="1" y2="0">
              <stop offset="0%"   stopColor="#67000d" />
              <stop offset="100%" stopColor="#fc8d59" />
            </linearGradient>
          </defs>

          <g transform={`translate(${MARGIN.left},${MARGIN.top})`}>

            <rect x={0} y={0} width={innerW} height={innerH} fill="none" stroke="#333" />

            {/* X axis */}
            <g transform={`translate(0,${innerH})`}>
              <line x2={innerW} stroke="#555" />
              {scales?.xTicks.map((t, i) => (
                <g key={i} transform={`translate(${t.x},0)`}>
                  <line y2={5} stroke="#555" />
                  <text y={18} textAnchor="middle" fontSize={11} fill="#aaa">{t.label}</text>
                </g>
              ))}
              <text x={innerW / 2} y={40} textAnchor="middle" fontSize={12} fill="#888">
                Time (HH:MM)
              </text>
            </g>

            {/* Y axis */}
            <g>
              <line y2={innerH} stroke="#555" />
              {scales?.yTicks.map((t, i) => (
                <g key={i} transform={`translate(0,${t.y})`}>
                  <line x2={-5} stroke="#555" />
                  <text x={-9} textAnchor="end" dominantBaseline="middle" fontSize={11} fill="#aaa">
                    {t.label}
                  </text>
                </g>
              ))}
              <text
                transform={`translate(-72,${innerH / 2}) rotate(-90)`}
                textAnchor="middle" fontSize={12} fill="#888"
              >
                Price (USD)
              </text>
            </g>

            {/* Bid legend */}
            <g transform={`translate(${innerW + 20}, 10)`}>
              <text fontSize={11} fill="#aaa" dy={-4}>Bid depth</text>
              <rect width={14} height={100} fill={`url(#${bidGradientId})`} />
              <text x={18} y={0}  fontSize={10} fill="#777" dominantBaseline="hanging">high</text>
              <text x={18} y={95} fontSize={10} fill="#777">low</text>
            </g>

            {/* Ask legend */}
            <g transform={`translate(${innerW + 20}, 130)`}>
              <text fontSize={11} fill="#aaa" dy={-4}>Ask depth</text>
              <rect width={14} height={100} fill={`url(#${askGradientId})`} />
              <text x={18} y={0}  fontSize={10} fill="#777" dominantBaseline="hanging">high</text>
              <text x={18} y={95} fontSize={10} fill="#777">low</text>
            </g>

          </g>
        </svg>
      </div>
    </div>
  )
}
