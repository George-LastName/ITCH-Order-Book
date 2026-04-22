import { useState } from 'react'
import StockSelector from './components/StockSelector.jsx'
import OrderBookHeatmap from './components/OrderBookHeatmap.jsx'

export default function App() {
  const [selection, setSelection] = useState({ table: null, stock: null })

  function handleSelect(table, stock) {
    // Only trigger a re-render (and re-fetch) when something actually changed
    setSelection(prev =>
      prev.table === table && prev.stock === stock
        ? prev
        : { table, stock }
    )
  }

  return (
    <div style={{ padding: '1.5rem' }}>
      <header style={{ marginBottom: '1.5rem' }}>
        <h1 style={{ fontSize: 20, fontWeight: 600, marginBottom: 4 }}>
          ITCH Order Book Heatmap
        </h1>
        <p style={{ fontSize: 13, color: '#666' }}>
          Bid (blue) and ask (red) depth over time. Colour intensity is log-scaled share depth.
        </p>
      </header>

      <div style={{ marginBottom: '1.5rem' }}>
        <StockSelector onSelect={handleSelect} />
      </div>

      {selection.table && selection.stock
        ? <OrderBookHeatmap table={selection.table} stock={selection.stock} />
        : <p style={{ color: '#555', fontSize: 13 }}>Select a dataset and stock above.</p>
      }
    </div>
  )
}
