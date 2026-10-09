import { memo, useReducer, version } from 'react';
import { createRoot } from 'react-dom/client';
import { flushSync } from 'react-dom';
import { buildData } from './data.js';

const listReducer = (state, action) => {
  const { data, selected } = state;
  switch (action.type) {
    case 'RUN': return { data: buildData(1000), selected: 0 };
    case 'RUN_LOTS': return { data: buildData(10000), selected: 0 };
    case 'ADD': return { data: data.concat(buildData(1000)), selected };
    case 'UPDATE': {
      const newData = data.slice(0);
      for (let i = 0; i < newData.length; i += 10) { const r = newData[i]; newData[i] = { id: r.id, label: r.label + ' !!!' }; }
      return { data: newData, selected };
    }
    case 'CLEAR': return { data: [], selected: 0 };
    case 'SWAP_ROWS': {
      const newdata = [...data];
      if (data.length > 998) { const d1 = newdata[1]; newdata[1] = newdata[998]; newdata[998] = d1; }
      return { data: newdata, selected };
    }
    case 'REMOVE': {
      const idx = data.findIndex((d) => d.id === action.id);
      return { data: [...data.slice(0, idx), ...data.slice(idx + 1)], selected };
    }
    case 'SELECT': return { data, selected: action.id };
    default: return state;
  }
};

const Row = memo(({ selected, item, dispatch }) => (
  <tr className={selected ? 'danger' : ''}>
    <td className="col-md-1">{item.id}</td>
    <td className="col-md-4"><a onClick={() => dispatch({ type: 'SELECT', id: item.id })}>{item.label}</a></td>
    <td className="col-md-1"><a className="remove" onClick={() => dispatch({ type: 'REMOVE', id: item.id })}><span className="glyphicon glyphicon-remove" aria-hidden="true" /></a></td>
    <td className="col-md-6" />
  </tr>
), (p, n) => p.selected === n.selected && p.item === n.item);

const Button = ({ id, cb, title }) => (
  <div className="col-sm-6 smallpad">
    <button type="button" className="btn btn-primary btn-block" id={id} onClick={cb}>{title}</button>
  </div>
);

const Main = () => {
  const [{ data, selected }, dispatch] = useReducer(listReducer, { data: [], selected: 0 });
  return (
    <div className="container">
      <div className="jumbotron"><div className="row">
        <div className="col-md-6"><h1>React keyed</h1></div>
        <div className="col-md-6"><div className="row">
          <Button id="run" title="Create 1,000 rows" cb={() => dispatch({ type: 'RUN' })} />
          <Button id="runlots" title="Create 10,000 rows" cb={() => dispatch({ type: 'RUN_LOTS' })} />
          <Button id="add" title="Append 1,000 rows" cb={() => dispatch({ type: 'ADD' })} />
          <Button id="update" title="Update every 10th row" cb={() => dispatch({ type: 'UPDATE' })} />
          <Button id="clear" title="Clear" cb={() => dispatch({ type: 'CLEAR' })} />
          <Button id="swaprows" title="Swap Rows" cb={() => dispatch({ type: 'SWAP_ROWS' })} />
        </div></div>
      </div></div>
      <table className="table table-hover table-striped test-data"><tbody>
        {data.map((item) => <Row key={item.id} item={item} selected={selected === item.id} dispatch={dispatch} />)}
      </tbody></table>
    </div>
  );
};

globalThis.__app = {
  name: 'React ' + version,
  mount(container) { const root = createRoot(container); flushSync(() => root.render(<Main />)); },
  flush() { flushSync(() => {}); },
};
