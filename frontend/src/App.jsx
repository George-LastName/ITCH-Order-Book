import { useState } from 'react'
import StockSelector from './components/StockSelector.jsx'
import OrderBookHeatmap from './components/OrderBookHeatmap.jsx'
import SnapshotHeatmap from './components/SnapshotHeatmap.jsx'

const sectionStyle = {
  marginBottom: '2.5rem',
}

const headingStyle = {
  fontSize: 15,
  fontWeight: 600,
  color: '#ccc',
  marginBottom: 10,
  paddingBottom: 6,
  borderBottom: '1px solid #2a2d3a',
}

export default function App() {
  const [selection, setSelection] = useState({ table: null, stock: null })

  function handleSelect(table, stock) {
    setSelection(prev =>
      prev.table === table && prev.stock === stock ? prev : { table, stock }
    )
  }

  // Snapshot table is the delta table with the suffix swapped
  const snapshotTable = selection.table
    ? selection.table.replace('_deltas', '_snapshots')
    : null

  return (
    <div style={{ padding: '1.5rem' }}>
      <header style={{ marginBottom: '1.5rem' }}>
        <h1 style={{ fontSize: 20, fontWeight: 600, marginBottom: 4 }}>
          ITCH Order Book Heatmap
        </h1>
        <p style={{ fontSize: 13, color: '#666' }}>
          Bid (blue→green) and ask (yellow→red) depth over time. Drag sliders or scroll to zoom. Hover for detail.
        </p>
      </header>

      <div style={{ marginBottom: '2rem' }}>
        <StockSelector onSelect={handleSelect} />
      </div>

      {selection.table && selection.stock ? (
        <>
          <div style={sectionStyle}>
            <div style={headingStyle}>
              Deltas — 1 s resolution, top 10 levels per side
            </div>
            <OrderBookHeatmap table={selection.table} stock={selection.stock} />
          </div>

          <div style={sectionStyle}>
            <div style={headingStyle}>
              Snapshots — 60 s resolution, top 10 levels per side
            </div>
            <SnapshotHeatmap snapshotTable={snapshotTable} stock={selection.stock} />
          </div>
        </>
      ) : (
        <p style={{ color: '#555', fontSize: 13 }}>Select a dataset and stock above.</p>
      )}
    </div>
  )
}
