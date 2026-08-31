/*
 * K Language standard library — base module
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module k;

namespace io {

/**
 * QueryParams — container for URI/HTTP query parameters (GET parameters).
 *
 * Provides parsing from application/x-www-form-urlencoded strings, key-value
 * inspection, modification with immutable "with*" factories, and serialization.
 * Keys and values are stored in decoded form.
 */
public class QueryParams {
private:
    _keys   : Vector<String>;
    _values : Vector<String>;

    static decodeSlice(src: const unsigned byte[], off: unsigned int, len: unsigned int) : String {
        if (len == 0u) {
            return String();
        }
        temp : unsigned byte[]! = new unsigned byte[len + 1u];
        outLen : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            b : unsigned byte = src[off + i];
            if (b == (unsigned byte) '+') {
                temp[outLen] = (unsigned byte) ' ';
                ++outLen;
                ++i;
            } else if (b == (unsigned byte) '%' && i + 2u < len) {
                h1 : int = HexDecoder::decodeNibble((byte) src[off + i + 1u]);
                h2 : int = HexDecoder::decodeNibble((byte) src[off + i + 2u]);
                if (h1 >= 0 && h2 >= 0) {
                    val : unsigned int = (((unsigned int) h1) << 4) | ((unsigned int) h2);
                    temp[outLen] = (unsigned byte) (val & 0xFFu);
                    ++outLen;
                    i += 3u;
                } else {
                    temp[outLen] = (unsigned byte) '%';
                    ++outLen;
                    ++i;
                }
            } else {
                temp[outLen] = b;
                ++outLen;
                ++i;
            }
        }
        res : unsigned byte[]! = new unsigned byte[outLen + 1u];
        CharHelpers::copyBytes(res, 0u, temp, 0u, outLen);
        res[outLen] = (unsigned byte) 0;
        delete temp;
        return String(res);
    }

    static encodeValue(val: const String&) : String {
        if (val.size() == 0u) {
            return String();
        }
        bytes : unsigned byte[]! = val.toUtf8();
        n : unsigned int = bytes.size;
        if (n <= 1u) {
            delete bytes;
            return String();
        }
        len : unsigned int = n - 1u;
        sb : StringBuilder;
        i : unsigned int = 0u;
        while (i < len) {
            b : unsigned byte = bytes[i];
            if (b == (unsigned byte) ' ') {
                sb.append("+");
            } else if (PercentEncoder::isUnreserved((byte) b)) {
                tmp : unsigned byte[]! = new unsigned byte[2];
                tmp[0] = b;
                tmp[1] = (unsigned byte) 0;
                sb.append(tmp);
                delete tmp;
            } else {
                ub : unsigned int = ((unsigned int) b) & 0xFFu;
                sb.append("%");
                c1 : byte = HexEncoder::encodeNibble(ub >> 4, true);
                c2 : byte = HexEncoder::encodeNibble(ub & 0x0Fu, true);
                tmp : unsigned byte[]! = new unsigned byte[3];
                tmp[0] = (unsigned byte) c1;
                tmp[1] = (unsigned byte) c2;
                tmp[2] = (unsigned byte) 0;
                sb.append(tmp);
                delete tmp;
            }
            ++i;
        }
        delete bytes;
        return sb.toString();
    }

    static parseInternal(target: QueryParams&, raw: const unsigned byte[]) {
        if (raw.size <= 1u) {
            return;
        }
        len : unsigned int = raw.size - 1u;
        pos : unsigned int = 0u;
        while (pos < len) {
            sep : int = -1;
            amp : int = -1;
            i : unsigned int = pos;
            while (i < len) {
                b : unsigned byte = raw[i];
                if (b == (unsigned byte) '&') {
                    amp = (int) i;
                    break;
                }
                if (b == (unsigned byte) '=' && sep < 0) {
                    sep = (int) i;
                }
                ++i;
            }
            partEnd : unsigned int = amp >= 0 ? (unsigned int) amp : len;
            if (partEnd > pos) {
                if (sep >= 0 && (unsigned int) sep >= pos && (unsigned int) sep < partEnd) {
                    kLen : unsigned int = ((unsigned int) sep) - pos;
                    vLen : unsigned int = partEnd - (((unsigned int) sep) + 1u);
                    kStr : String = decodeSlice(raw, pos, kLen);
                    vStr : String = decodeSlice(raw, ((unsigned int) sep) + 1u, vLen);
                    target.add(kStr, vStr);
                } else {
                    kLen : unsigned int = partEnd - pos;
                    kStr : String = decodeSlice(raw, pos, kLen);
                    target.add(kStr, String());
                }
            }
            if (amp >= 0) {
                pos = ((unsigned int) amp) + 1u;
            } else {
                break;
            }
        }
    }

public:
    /** Construct an empty QueryParams container. */
    QueryParams() {}

    /** Construct by parsing a query string. */
    QueryParams(queryString: const String&) {
        bytes : unsigned byte[]! = queryString.toUtf8();
        parseInternal(this, bytes);
        delete bytes;
    }

    /** Construct by parsing a character array. */
    QueryParams(queryString: const char[]) {
        str : String(queryString);
        bytes : unsigned byte[]! = str.toUtf8();
        parseInternal(this, bytes);
        delete bytes;
    }

    /** Construct by parsing an unsigned byte array. */
    QueryParams(queryString: const unsigned byte[]) {
        parseInternal(this, queryString);
    }

    /** Copy constructor. */
    QueryParams(other: const QueryParams&) {
        i : unsigned int = 0u;
        n : unsigned int = other._keys.size();
        while (i < n) {
            _keys.append(other._keys[i]);
            _values.append(other._values[i]);
            ++i;
        }
    }

    /** Static factory for an empty container. */
    static of() : QueryParams! {
        return new QueryParams();
    }

    /** Static factory parsing from a String. */
    static parse(queryString: const String&) : QueryParams! {
        qp : QueryParams! = new QueryParams();
        bytes : unsigned byte[]! = queryString.toUtf8();
        parseInternal(qp, bytes);
        delete bytes;
        return qp;
    }

    /** Static factory parsing from a char array. */
    static parse(queryString: const char[]) : QueryParams! {
        qp : QueryParams! = new QueryParams();
        str : String(queryString);
        bytes : unsigned byte[]! = str.toUtf8();
        parseInternal(qp, bytes);
        delete bytes;
        return qp;
    }

    /** Static factory parsing from an unsigned byte array. */
    static parse(queryString: const unsigned byte[]) : QueryParams! {
        qp : QueryParams! = new QueryParams();
        parseInternal(qp, queryString);
        return qp;
    }

    /** Return the total number of key/value pairs. */
    const size() : unsigned int {
        return _keys.size();
    }

    /** Return true if there are no query parameters. */
    const isEmpty() : bool {
        return _keys.size() == 0u;
    }

    /** Return true if at least one parameter with the given key exists. */
    const has(name: const String&) : bool {
        i : unsigned int = 0u;
        n : unsigned int = _keys.size();
        while (i < n) {
            if (_keys[i] == name) return true;
            ++i;
        }
        return false;
    }

    /** Return true if at least one parameter with the given key exists. */
    const has(name: const char[]) : bool {
        return has(String(name));
    }

    /** Return the first value associated with `name`, or an empty String if not found. */
    const get(name: const String&) : String {
        i : unsigned int = 0u;
        n : unsigned int = _keys.size();
        while (i < n) {
            if (_keys[i] == name) {
                return _values[i];
            }
            ++i;
        }
        return String();
    }

    /** Return the first value associated with `name`, or an empty String if not found. */
    const get(name: const char[]) : String {
        return get(String(name));
    }

    /** Return all values associated with `name`. */
    const getAll(name: const String&) : Vector<String>! {
        res : Vector<String>! = new Vector<String>();
        i : unsigned int = 0u;
        n : unsigned int = _keys.size();
        while (i < n) {
            if (_keys[i] == name) {
                res.append(_values[i]);
            }
            ++i;
        }
        return res;
    }

    /** Return all values associated with `name`. */
    const getAll(name: const char[]) : Vector<String>! {
        return getAll(String(name));
    }

    /** Return the key at index `index`. */
    const keyAt(index: unsigned int) : String {
        return _keys[index];
    }

    /** Return the value at index `index`. */
    const valueAt(index: unsigned int) : String {
        return _values[index];
    }

    /** Return a list of all distinct keys in insertion order. */
    const keys() : Vector<String>! {
        res : Vector<String>! = new Vector<String>();
        i : unsigned int = 0u;
        n : unsigned int = _keys.size();
        while (i < n) {
            k : String = _keys[i];
            exists : bool = false;
            j : unsigned int = 0u;
            while (j < res.size()) {
                if (res.get(j) == k) {
                    exists = true;
                    break;
                }
                ++j;
            }
            if (!exists) {
                res.append(k);
            }
            ++i;
        }
        return res;
    }

    /** Append a key/value pair. */
    add(name: const String&, value: const String&) {
        _keys.append(name);
        _values.append(value);
    }

    /** Append a key/value pair. */
    add(name: const char[], value: const char[]) {
        add(String(name), String(value));
    }

    /** Set a parameter: replaces the first occurrence and removes subsequent ones, or appends. */
    set(name: const String&, value: const String&) {
        found : bool = false;
        i : unsigned int = 0u;
        while (i < _keys.size()) {
            if (_keys[i] == name) {
                if (!found) {
                    _values[i] = value;
                    found = true;
                    ++i;
                } else {
                    _keys.removeAt(i);
                    _values.removeAt(i);
                }
            } else {
                ++i;
            }
        }
        if (!found) {
            add(name, value);
        }
    }

    /** Set a parameter. */
    set(name: const char[], value: const char[]) {
        set(String(name), String(value));
    }

    /** Remove all occurrences of the given parameter name. */
    remove(name: const String&) {
        i : unsigned int = 0u;
        while (i < _keys.size()) {
            if (_keys[i] == name) {
                _keys.removeAt(i);
                _values.removeAt(i);
            } else {
                ++i;
            }
        }
    }

    /** Remove all occurrences of the given parameter name. */
    remove(name: const char[]) {
        remove(String(name));
    }

    /** Clear all parameters. */
    clear() {
        _keys.clear();
        _values.clear();
    }

    /** Return a new QueryParams with the added parameter. */
    const withParam(name: const String&, value: const String&) : QueryParams! {
        cp : QueryParams! = new QueryParams(this);
        cp.add(name, value);
        return cp;
    }

    /** Return a new QueryParams with the added parameter. */
    const withParam(name: const char[], value: const char[]) : QueryParams! {
        return withParam(String(name), String(value));
    }

    /** Return a new QueryParams with the parameter set (replacing existing). */
    const withSetParam(name: const String&, value: const String&) : QueryParams! {
        cp : QueryParams! = new QueryParams(this);
        cp.set(name, value);
        return cp;
    }

    /** Return a new QueryParams with the parameter set. */
    const withSetParam(name: const char[], value: const char[]) : QueryParams! {
        return withSetParam(String(name), String(value));
    }

    /** Return a new QueryParams without any occurrence of `name`. */
    const withoutParam(name: const String&) : QueryParams! {
        cp : QueryParams! = new QueryParams(this);
        cp.remove(name);
        return cp;
    }

    /** Return a new QueryParams without any occurrence of `name`. */
    const withoutParam(name: const char[]) : QueryParams! {
        return withoutParam(String(name));
    }

    /** Serialize the query parameters to an application/x-www-form-urlencoded String. */
    const toString() : String {
        if (_keys.size() == 0u) {
            return String();
        }
        sb : StringBuilder;
        i : unsigned int = 0u;
        n : unsigned int = _keys.size();
        while (i < n) {
            if (i > 0u) {
                sb.append("&");
            }
            kEnc : String = encodeValue(_keys[i]);
            sb.append(kEnc);
            vStr : String = _values[i];
            if (vStr.size() > 0u) {
                sb.append("=");
                vEnc : String = encodeValue(vStr);
                sb.append(vEnc);
            }
            ++i;
        }
        return sb.toString();
    }

    /** Serialize the query parameters to UTF-8 bytes. */
    const toUtf8() : unsigned byte[]! {
        s : String = toString();
        return s.toUtf8();
    }
}

} // namespace io
