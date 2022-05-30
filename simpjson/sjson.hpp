#include <variant>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

#include <utility> // std::move

#include <exception>

#include <sstream>

#include <iomanip> // setw

#include <functional>

#undef max

namespace sjson {

namespace _sjson_detail
{
	constexpr auto max_uint32 = static_cast<uint32_t>(-1);

	namespace utf8
	{
		/**
		 * \param u Uniocde 码
		 * \return 对 u 进行编码所需的字节数（无效返回 0）
		 */
		constexpr int get_byte_num_of_encode(uint32_t u)
		{
			if (u <= 0x7f)return 1;
			if (u <= 0x7ff)return 2;
			if (u <= 0xffff)return 3;
			if (u <= 0x10ffff)return 4;
			return 0;
		}
		/**
		 * \param byte 序列的最高字节（开头）
		 * \return 需要进行解码的字节数（如非最高字节则返回 0）
		 */
		constexpr int get_byte_num_of_decode(uint8_t byte)
		{
			if ((byte & 0xc0) == 0x80)return 0;
			if ((byte & 0xf8) == 0xf0)return 4;
			if ((byte & 0xf0) == 0xe0)return 3;
			if ((byte & 0xe0) == 0xc0)return 2;
			return 1;
		}

		constexpr uint32_t decode(const uint8_t dest[], size_t len)
		{
			if (*dest <= 0x7f)return *dest;
			uint32_t res = 0, restbyte_cnt = 0, i = 0;
			if ((*dest & 0xe0) == 0xc0)
			{
				res = *dest & 0x1f;
				restbyte_cnt = 1;
			}
			else if ((*dest & 0xf0) == 0xe0)
			{
				res = *dest & 0x0f;
				restbyte_cnt = 2;
			}
			else if ((*dest & 0xf8) == 0xf0)
			{
				res = *dest & 0x07;
				restbyte_cnt = 3;
			}
			else return max_uint32;

			if (restbyte_cnt > len - 1)return max_uint32;

			while (restbyte_cnt > 0)
			{
				i++;
				restbyte_cnt--;

				// 高两位必须为 10
				if ((dest[i] & 0xc0) != 0x80)return max_uint32;

				res = (res << 6) | (dest[i] & 0x3f);
			}
			return res;
		}

		constexpr uint8_t encode(uint8_t out[], uint32_t u)
		{
			if (u <= 0x7f)
			{
				*out = u;
				return 1;
			}
			else if (u <= 0x7ff)
			{
				out[0] = 0xc0 | ((u & 0x7c0) >> 6); // 高字节
				out[1] = 0x80 | (u & 0x03f); // 低字节
				return 2;
			}
			else if (u <= 0xffff)
			{
				out[0] = 0xe0 | ((u & 0xf000) >> 12); // 高字节
				out[1] = 0x80 | ((u & 0xfc0) >> 6); // 中间字节
				out[2] = 0x80 | (u & 0x3f); // 低字节
				return 3;
			}
			else if (u <= 0x10ffff)
			{
				out[0] = 0xf0 | ((u & 0x1c0000) >> 18);
				out[1] = 0x80 | ((u & 0x2f000) >> 12);
				out[2] = 0x80 | ((u & 0xfc0) >> 6);
				out[3] = 0x80 | (u & 0x3f);
				return 4;
			}
			return 0;
		}
	};

	void escape_to_ascii(const std::string& s, std::string& out)
	{
		std::stringstream ss;
		ss << std::hex;
		ss.fill('0');
		for (size_t i = 0; i < s.size(); ++i)
		{
			auto byte = static_cast<uint8_t>(s[i]);
			auto byte_cnt = utf8::get_byte_num_of_decode(byte);

			if (byte_cnt == 1)
			{
				switch (byte)
				{
				case '\b': ss<<"\\b"; break;
				case '\t': ss<<"\\t"; break;
				case '\n': ss<<"\\n"; break;
				case '\f': ss<<"\\f"; break;
				case '\r': ss<<"\\r"; break;
				case '\"':ss << "\\\""; break;
				case '\\':ss << "\\\\"; break;
				default:
					ss << s[i];
					break;
				}
			}
			else
			{
				auto u = utf8::decode((const uint8_t*)s.data() + i, s.size() - i);
				ss << "\\u" << std::setw(4);
				if (u <= 0xffff)ss << u;
				else
				{
					ss << (0xd7c0u + (u >> 10))
						<< std::setw(0) << "\\u" << std::setw(4)
						<< (0xdc00u + (u & 0x3ffu));
				}
				ss.width(0);
				i += byte_cnt - 1;
			}
		}
		out += ss.str();
	}

	enum class parser_delimiter :uint32_t
	{
		comma = ',',
		colon = ':',
		left_bracket = '[',
		right_bracket = ']',
		left_brace = '{',
		right_brace = '}'
	};

	template<typename _json_t>
	class parser;

};

enum class json_value_t
{
	array,
	object,

	null,
	num_double,
	num_i32,
	boolean,
	string,

	num_ui32, num_i64, num_ui64
};

constexpr inline const char* json_type_name(json_value_t x)
{
	return x == json_value_t::array
		? "array"
		: (x == json_value_t::object ? "object" : "value");
}
constexpr const char* json_value_t_name(json_value_t x)
{
	constexpr const char* name_of[] =
	{
		"array", "object",
		
		"null",

		"num.double", "num.int32", "num.boolean",
		"string",

		"num.uint32","num.int64","num.uint64",

		"parser.delimiter"
	};

	return static_cast<size_t>(x) < 11
		? name_of[static_cast<size_t>(x)]
		: "unknown";
}

enum class json_parse_error
{
	item_not_closed, // <注释/字符串/数组/对象> 未闭合，具体取决于 origin
	illegal_escape, // 不符合规定的 <unicode/\xxx> 转义序列
	invalid_unicode_code, // 无效的 unicode 码
	unknown_keyword, // 未知的的关键词
	/*
	* msg 格式:
	*   xxxxx@yyyyyy
	* 以第一个 '@' 为分隔符
	* 前面是期望值(<值类型> 或 {正则表达式}) 后面是实际值
	*/
	unexpected_item, // 意外的值/对象/字符
};

enum class json_callback_ret
{
	ignore_and_continue,
	abort
};
enum class json_error_origin
{
	parse_comment,
	parse_string,
	parse_unicode,
	parse_keyword,
	parse_delimiter,
	parse_array,
	parse_object
};

using json_parse_error_callback_f = std::function<
	json_callback_ret(
		uint32_t line, uint32_t column,
		json_error_origin origin,
		json_parse_error e, const std::string& msg
		)
>;

namespace _sjson_detail
{
	json_callback_ret defult_parse_err_callback(
		uint32_t line, uint32_t column,
		json_error_origin origin,
		json_parse_error e, const std::string& msg
	);
}

/*
* ecode:
* 0 try failed
* 1 bad call: call xxx method on yyy
*/

class json_error :public std::exception
{
public:
	
	json_error(const char* s, int ec) :_what(s), _err_code(ec) {}
	json_error(const std::string& s, int ec = 0) :_what(s), _err_code(ec) {}
	json_error(int line, const char* func, const std::string& str, int ec)
		: _what(std::string("sjson.hpp:") + std::to_string(line) +
			':' + func + (ec ? ":[json_error." + std::to_string(ec) + "] " : ": ")  + str), _err_code(ec) {}

	int error_code()const { return _err_code; }

	virtual const char* what()const
	{
		return _what.c_str();
	}

private:
	int _err_code;
	std::string _what;
};

#pragma region DEFINES

#define _JSON_THROW(str,err_code)  throw json_error(__LINE__,__func__,str,err_code)

#define _JSON_TRY_(expr,err_code)                        \
    do                                                   \
    {                                                    \
        try                                              \
        {                                                \
            expr;                                        \
        }                                                \
        catch (const json_error &e)                      \
        {                                                \
			_JSON_THROW(								 \
				std::string(""#expr" failed\n  > ")		 \
				+ e.what(),err_code						 \
			);											 \
        }                                                \
    } while (0)

#define _JSON_TRY(expr) _JSON_TRY_(expr,0)

/*
* 禁用意外类型的自动类型调整
* 如：对 array 对象使用 ["key"]
*   如果是 const array 则会返回空的 json
*   如果是 array 则会将该结点转换成 object 再返回
*/
//#define _SJSON_DISABLE_AUTO_TYPE_ADJUST

#if defined _SJSON_DISABLE_AUTO_TYPE_ADJUST

#endif

#ifndef _SJSON_DISABLE_AUTO_TYPE_ADJUST
#define _JSON_THROW_TYPE_ADJUST(dest, need) ((void)0)
#define _JSON_THROW_TYPE_ADJUST_RAW(dest, need) ((void)0)
#else
#define _JSON_THROW_TYPE_ADJUST_RAW(dest, need) \
_JSON_THROW(\
	std::string("call method of json::") + need + " on json::" + dest + " (auto type adjust is disabled)",\
	1\
)
#define _JSON_THROW_TYPE_ADJUST(dest, need) \
_JSON_THROW_TYPE_ADJUST_RAW(json_value_t_name(dest), json_value_t_name(need))
#endif

#define _JSON_ENSURE_IS(x) _JSON_TRY(_ensure_is<x>())

#pragma endregion

template<
	typename _string_t = std::string,
	// 后面必须要有 typename ... 之类的东西（用来满足 vector 和 map 的模板参数）否则会导致被其他模板使用时编译失败
	template<typename _key_t, typename _val_t, typename ...> typename _map_t = std::unordered_map,
	template<typename _val_t, typename ...> typename _arr_t = std::vector
>
class _basic_json
{
private:

	template <class _list, class _t>
	struct _meta_find_idx {
		static constexpr size_t value = -1;
	};

	static constexpr size_t _meta_find_idx_func(const bool* const _Ptr, const size_t _Count,
		size_t _Idx = 0) {

		for (; _Idx < _Count; ++_Idx) {
			if (_Ptr[_Idx]) {
				return _Idx;
			}
		}
		return -1;
	}

	template <template <class...> class _list, class _first, class... _rest, class _t>
	struct _meta_find_idx<_list<_first, _rest...>, _t>
	{
		static constexpr bool _bools[] = { std::is_same_v<_first, _t>, std::is_same_v<_rest, _t>... };
		static constexpr size_t value = _meta_find_idx_func(_bools, 1 + sizeof...(_rest));
	};

	class _my_initializer_list;

public:

	using string_t = _string_t;
	using string_char_t = typename _string_t::value_type;

	using array_t = _arr_t<_basic_json>;
	using object_t = _map_t<string_t, _basic_json>;

private:

	std::variant<
		array_t, object_t,
		nullptr_t, double, int32_t, bool,
		string_t,
		uint32_t, int64_t, uint64_t,

		_sjson_detail::parser_delimiter
	> _data;

	static constexpr json_value_t _json_value_parser_delimiter = static_cast<json_value_t>(10);

	template<typename _t>
	using _enable_if_can_assign = typename std::enable_if<
		!std::is_same<_t, decltype(_data)>::value &&
		std::is_constructible<decltype(_data), _t>::value,
	int>::type;

	size_t _type_raw() const { return _data.index(); }

public:
		
	/**
	 * @brief 初始化为一个空的 json
	*/
	_basic_json() :_data(nullptr) {}
		
	/**
	 * @brief 使用 x 初始化 json
	 * @tparam _t 可接受的数据类型（详见 json_value_t）
	 * @param x 用于初始化的值
	*/
	template <typename _t, _enable_if_can_assign<_t> = 0>
	_basic_json(const _t& x) { assign(x); }

	_basic_json(const std::initializer_list<_my_initializer_list>& x)
	{
		assign(_my_initializer_list(x).data());
	}

	_basic_json(json_value_t t)
	{
		switch (t)
		{
		case json_value_t::array: assign(array_t()); break;
		case json_value_t::object: assign(object_t()); break;
		case json_value_t::null: assign(nullptr); break;
		case json_value_t::num_double: assign(0.0); break;
		case json_value_t::num_i32: assign(0); break;
		case json_value_t::boolean: assign(false); break;
		case json_value_t::string: assign(_string_t()); break;
		case json_value_t::num_ui32: assign(0U); break;
		case json_value_t::num_i64: assign(0LL); break;
		case json_value_t::num_ui64:assign(0ULL); break;
		default:
			break;
		}
	}

	json_value_t type()const { return static_cast<json_value_t>(_type_raw()); }
	
	const char* type_name()const { return json_type_name(type()); }
	const char* value_t_name()const { return json_value_t_name(type()); }


	/**
	 * @brief 清空当前的数据（设为 nullptr）
	*/
	void clear() { _data = nullptr; }

	// assign
	#pragma region assign

	/**
	 * @brief 初始化为空
	*/
	void assign() { clear(); }
	
	/**
	 * @brief 通过 可被接受的数据 构造
	 * @tparam _t 可接受的数据类型（详见 json_value_t）
	 * @param x 可接受的数据
	*/
	template <typename _t, _enable_if_can_assign<_t> = 0>
	void assign(const _t& x) { _data = x; }
	
	/**
	 * @brief 通过 字符指针/数组 构造
	 * @param str 字符串
	*/
	void assign(const string_char_t* str) // 防止 char* 被当成 bool 错误调用
	{
		_data = std::move(string_t(str));
	}

	/**
	 * @brief 通过 另一个 json 值 构造
	 * @param x 指定的 json
	*/
	void assign(const _basic_json& x) { _data = x._data; }

	#pragma endregion

	const _basic_json& operator=(const _basic_json& x)
	{
		assign(x);
		return x;
	}

	// hold

	/**
	 * @brief 检查持有类型是否为 _t
	 * @tparam _t 要检查的类型
	 * @return 是否持有
	*/
	template<typename _t>
	bool hold()const noexcept { return std::holds_alternative<_t>(_data); }
	
	/**
	 * @brief 检查持有类型的编号是否为 _idx
	 * @return 是否持有
	*/
	template<json_value_t _idx>
	bool hold()const noexcept
	{
		return hold<
			std::variant_alternative_t<
				static_cast<size_t>(_idx), decltype(_data)
			>
		>();
	}

	/**
	 * @brief 检查是否持有 值 类型
	 * @return 是否持有
	 */
	bool hole_value_type()const noexcept
	{
		auto t = type();
		if (t == json_value_t::array
			|| t == json_value_t::object
			|| t == _json_value_parser_delimiter
			)return false;
		return true;
	}

	// get
	#pragma region get

#define _TYPE_OF_IDX std::variant_alternative_t<static_cast<size_t>(_idx), decltype(_data)>

	/**
	 * @brief 尝试获取指定类型数据
	 * @tparam _t 类型
	 * @return 指向目标数据的指针，如持有类型非 _t 则返回空指针
	*/
	template<typename _t>
	_t* get_if() { return std::get_if<_t>(&_data); }
	/**
	 * @brief 尝试获取指定类型数据
	 * @tparam _t 类型
	 * @return 指向目标数据的指针，如持有类型非 _t 则返回空指针
	*/
	template<typename _t>
	const _t* get_if()const { return std::get_if<_t>(&_data); }
	/**
	 * @brief 尝试获取指定类型数据
	 * @tparam _idx 类型的编号
	 * @return 指向目标数据的指针，如持有类型非 _idx 所对应则返回空指针
	*/
	template<json_value_t _idx>
	_TYPE_OF_IDX* get_if() { return std::get_if<static_cast<size_t>(_idx)>(&_data); }
	/**
	 * @brief 尝试获取指定类型数据
	 * @tparam _idx 类型的编号
	 * @return 指向目标数据的指针，如持有类型非 _idx 所对应则返回空指针
	*/
	template<json_value_t _idx>
	const _TYPE_OF_IDX* get_if()const { return std::get_if<static_cast<size_t>(_idx)>(&_data); }

	// get<_t>

	/**
	 * @brief 获取指定类型数据
	 * @tparam _t 类型
	 * @return 目标数据，如持有类型非 _t 则将当前值调整为 _t 后返回。（定义 _SJSON_DISABLE_AUTO_TYPE_ADJUST 则会抛出错误代替调整）
	*/
	template<typename _t>
	_t& get()
	{
		_JSON_ENSURE_IS(_t);
		return std::get<_t>(_data);
	}
	/**
	 * @brief 获取指定类型数据
	 * @tparam _t 类型
	 * @return 目标数据，如持有类型非 _t 则返回一个静态常量值的引用。（定义 _SJSON_DISABLE_AUTO_TYPE_ADJUST 则会抛出错误）
	*/
	template<typename _t>
	const _t& get()const
	{
		_JSON_ENSURE_IS(_t);
		return hold<_t>() ? std::get<_t>(_data) : _make_tmp<_t>();
	}

	// get<_idx>

	/**
	 * @brief 获取指定类型数据
	 * @tparam _idx 类型的序号
	 * @return 目标数据，如持有类型非 _t 则将当前值调整为 _idx 所对应类型后返回。（定义 _SJSON_DISABLE_AUTO_TYPE_ADJUST 则会抛出错误代替调整）
	*/
	template<json_value_t _idx>
	inline _TYPE_OF_IDX& get()
	{
		return get<_TYPE_OF_IDX>();
	}
	/**
	 * @brief 获取指定类型数据
	 * @tparam _idx 类型的序号
	 * @return 目标数据，如持有类型非 _idx 所对应类型则返回一个静态常量值的引用。（定义 _SJSON_DISABLE_AUTO_TYPE_ADJUST 则会抛出错误）
	*/
	template<json_value_t _idx>
	inline const _TYPE_OF_IDX& get()const
	{
		return get<_TYPE_OF_IDX>();
	}

#undef _TYPE_OF_IDX

	#pragma endregion

	#pragma region operator

	// operator ==

	bool operator == (const _basic_json& j)const
	{
		return _data == j._data;
	}
	template <typename _t, _enable_if_can_assign<_t> = 0>
	bool operator == (const _t& val)const
	{
		return _data == _basic_json(val)._data;
	}

	// operator _t

	inline operator const array_t& () const { return get<array_t>(); }
	inline operator const object_t& () const { return get<object_t>(); }
	template <
		typename _t,
		typename std::enable_if_t<
		!(
			// 消除 string 歧义
			std::is_pointer_v<_t> // [const] char* [const]
			|| std::is_same_v<_t, string_char_t>
			|| std::is_same_v<_t, std::initializer_list<string_char_t> >
			), int
		> = 0
	> inline operator _t () const { return get<_t>(); }

	// operator []
	
	// 如果超出数组范围，会更改大小
	_basic_json& operator[](size_t idx)
	{
		_JSON_TRY(get<array_t>());
		auto& arr = get<array_t>();
		if (idx >= arr.size())arr.resize(idx + 1);
		return arr[idx];
	}
	const _basic_json& operator[](size_t idx)const
	{
		_JSON_TRY(return get<array_t>()[idx]);
	}

	_basic_json& operator[](const string_t& key)
	{
		_JSON_TRY(return get<object_t>()[key]);
	}
	const _basic_json& operator[](const string_t& key)const
	{
		_JSON_TRY(get<object_t>());
		const auto& obj = get<object_t>();
		const auto& it = obj.find(key);
		return (it == obj.end()) ? _make_tmp<_basic_json>() : it->second;
	}

	// 不使用 string_char_t* 以防 0 被错误识别

	template <typename _t>
	_basic_json& operator[](const _t* const key)
	{
		return this->operator[](std::move(string_t(key)));
	}
	template <typename _t>
	const _basic_json& operator[](const _t* const key)const
	{
		return this->operator[](std::move(string_t(key)));
	}

#pragma endregion

	/**
	 * \param out 用于存放格式化后的字符串
	 * \param tabstop 缩进长度
	 * \param space 缩进字符
	 * \param ensure_ascii 是否需要转换成 ASCII 格式（字符串会被强行当成 UTF-8 格式处理）
	 */
	void dump_to(std::string& out, int tabstop = -1, char space = ' ', bool ensure_ascii = true)const
	{
		if (tabstop < 0)tabstop = 4;
		_dump_to(out, tabstop, space, ensure_ascii);
	}
	/** 
	 * \param tabstop 缩进长度
	 * \param space 缩进字符
	 * \param ensure_ascii 是否需要转换成 ASCII 格式（字符串会被强行当成 UTF-8 格式处理）
	 * \return 格式化结果
	 */
	std::string dump(int tabstop = -1, char space = ' ', bool ensure_ascii = true)const
	{
		std::string res;
		dump_to(res, tabstop, space, ensure_ascii);
		return res;
	}

	friend std::istream& operator>>(std::istream& is, _basic_json& j)
	{
		_sjson_detail::parser<_basic_json>(is).get_result_to(j);
		return is;
	}

	void push_back(const _basic_json& j)
	{
		if (hold<array_t>())get<array_t>().push_back(j);
		else
		{

		}
	}

	size_t size()
	{
		if (hold<array_t>())return get<array_t>().size();
		else if (hold<object_t>())return get<object_t>().size();
		// TOOD 抛出错误不能对值使用 size()
		return 0;
	}

private:

	template<typename _t>
	static inline const _t& _make_tmp()
	{
		static const _t tmp = _t();
		return tmp;
	}

	template<json_value_t _idx>
	bool _ensure_is()
	{
		using _t = std::variant_alternative_t<static_cast<size_t>(_idx), decltype(_data)>;
		if (!hold<_t>())
		{
			if (!hold<nullptr_t>())
			{
				_JSON_THROW_TYPE_ADJUST(type(), _idx);
			}
			_data = _t();
		}
		return true;
	}
	template<typename _t>
	bool _ensure_is()
	{
		return _ensure_is<
			static_cast<json_value_t>(_meta_find_idx<decltype(_data), _t>::value)
		>();
	}

	template<json_value_t _idx>
	bool _ensure_is()const
	{
		if (!hold<_idx>())
		{
			if (hold<nullptr_t>())return false;
			_JSON_THROW_TYPE_ADJUST(type(), _idx);
			// 如果没有定义 _SJSON_DISABLE_AUTO_TYPE_ADJUST 则该语句无效，会在调用 get 处调整
		}
		return true;
	}
	template<typename _t>
	bool _ensure_is()const
	{
		return _ensure_is<
			static_cast<json_value_t>(_meta_find_idx<decltype(_data), _t>::value)
		>();
	}

	class _my_initializer_list
	{
	public:
	
		_my_initializer_list(const _basic_json& x) :_data(x) {}
	
		/*
		* {a,b,c} : 这种情况会对每一个元素调用该函数来初始化
		*  ^
		*/
		template <typename _t>
		_my_initializer_list(const _t& x) : _data(x) {}
		/*
		* (a,b,c) 或 {}
		* ^^^^^^^
		* 会调用该函数来初始化
		*/
		template <typename... _ts>
		_my_initializer_list(const _ts &...args) : _data(array_t({ _basic_json(args)... })) {}
	
		_my_initializer_list(std::initializer_list<_my_initializer_list> x)
		{
			/*
			* 形如 {{"xxx",...},{"yyy",...},...} 的数据会被视为 object
			*/
			bool is_obj = true;
			for (auto& it : x)
			{
				const auto& node = it._data;
				if ((!node.hold<array_t>()) || node.get<array_t>().size() != 2)
				{
					is_obj = false;
					break;
				}
				if (!node.get<array_t>()[0].hold<string_t>())
				{
					is_obj = false;
					break;
				}
			}
			if (is_obj)
			{
				_data.assign(object_t());
				auto& dest = _data.get<object_t>();
				for (auto& it : x)
				{
					dest.insert(
						{
							it._data.get<array_t>()[0].get<std::string>(),
							it._data.get<array_t>()[1]
						}
					);
				}
				return;
			}
			_data.assign(array_t());
			auto& dest = _data.get<array_t>();
			for (auto& it : x)
			{
				dest.push_back(it._data);
			}
		}
	
		_basic_json& data()& { return _data; }
		const _basic_json& data() const& { return _data; }
	
		_basic_json&& data()&& { return std::move(_data); }
	
	private:
		_basic_json _data;
	};

	void _dump_to(std::string& out, int tabstop, char space, bool ensure_ascii, int deep = 0)const
	{
		bool need_format = tabstop > 0;
		auto add_tabs = [&tabstop, &space, &out, &deep]()
		{
			out += std::string(deep * tabstop, space);
		};
		auto dump_string = [&out, &ensure_ascii](const std::string& s)
		{
			out += '"';
			if (!ensure_ascii)out += s;
			else _sjson_detail::escape_to_ascii(s, out);

			out += '"';
		};

		static constexpr const char delimiter_ch[] = "?,:[]{}";

		using std::to_string;

		switch (type())
		{
		case json_value_t::array:
		{
			const auto& arr = get<array_t>();

			out += '[';
			if (arr.size() == 1 && arr[0].hole_value_type())
			{
				arr[0]._dump_to(out, tabstop, space, ensure_ascii, deep);
			}
			else if(!arr.empty())
			{
				deep++;
				if (need_format)out += '\n';
				for (auto it = arr.begin(); it != arr.end(); ++it)
				{
					if (it != arr.begin())
						out += need_format ? ",\n" : ",";
					add_tabs();
					it->_dump_to(out, tabstop, space, ensure_ascii, deep);
				}
				deep--;
				if (need_format)
				{
					out += '\n';
					add_tabs();
				}
			}
			out += ']';
			break;
		}
		case json_value_t::object:
		{
			const auto& obj = get<object_t>();
			
			out += '{';
			if (obj.size() == 1 && obj.begin()->second.hole_value_type())
			{
				const auto& it = obj.begin();
				dump_string(it->first);

				out += ':';
				if (need_format)out += ' ';

				it->second._dump_to(out, tabstop, space, ensure_ascii, deep);
			}
			else if (!obj.empty())
			{
				if (need_format)
					out += '\n';
				deep++;
				for (auto it = obj.begin(); it != obj.end(); ++it)
				{
					if (it != obj.begin())
						out += need_format ? ",\n" : ",";
					add_tabs();
					dump_string(it->first);

					out += ':';
					if (need_format)out += ' ';

					it->second._dump_to(out, tabstop, space, ensure_ascii, deep);
				}
				deep--;
				if (need_format)
				{
					out += '\n';
					add_tabs();
				}
			}
			out += '}';
			break;
		}
		case json_value_t::null:out += "null"; break;
		case json_value_t::boolean:out += (get<bool>() ? "true" : "false"); break;
		case json_value_t::string:dump_string(get<std::string>()); break;
		case json_value_t::num_double:
		{
			std::stringstream ss;
			double d = get<double>();
			ss << std::defaultfloat << d;
			string_t s = ss.str();
			s.erase(std::remove(s.begin(), s.end(), '+'), s.end());
			out += s;
			break;
		}
		case json_value_t::num_i32:out += to_string(get<int>()); break;
		case json_value_t::num_ui32:out += to_string(get<uint32_t>()); break;
		case json_value_t::num_i64:out += to_string(get<int64_t>()); break;
		case json_value_t::num_ui64:out += to_string(get<uint64_t>()); break;

		case _json_value_parser_delimiter:
		{
			out.push_back(
				static_cast<string_char_t>(
					get<_sjson_detail::parser_delimiter>()
				)
			);
		}

			break;
		default:
			out += "null";
			break;
		}
	}


};

namespace _sjson_detail
{
	template<typename _json_t>
	class parser
	{
	private:

		using _error = json_parse_error;
		using _origin = json_error_origin;

		using _char_t = _json_t::string_char_t;
		using _str_t = _json_t::string_t;

	public:

		static constexpr _char_t end_flag = 0;
		using get_f = std::function<_char_t()>;

	private:

		_char_t _cur_ch;
		_char_t _next_ch;
		get_f _getch_func;

		struct
		{
			uint32_t line = 0, column = 0;
		}_cur_pos, _cur_node_start_pos;


		_json_t _cur_node;

		bool _is_abort = false;
		json_parse_error_callback_f _err_callback;

		void _throw_err(
			json_error_origin origin,
			json_parse_error e, const std::string& msg = ""
		)
		{
			if (_err_callback)
			{
				if (
					_err_callback(
						_cur_node_start_pos.line,
						_cur_node_start_pos.column,
						origin, e, msg
					) == json_callback_ret::abort
					) {
					_cur_ch = _next_ch = end_flag;
					_is_abort = true;
				}
			}
		}

		constexpr static bool _isdigit(_char_t ch, int base = 10)
		{
			return ('0' <= ch && ch <= '9')
				|| ('A' <= ch && ch < 'A' + (base -= 10))
				|| ('a' <= ch && ch < 'a' + base);
		}
		constexpr static bool _isalpha(_char_t ch)
		{
			return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z');
		}
		constexpr static bool _isalnum(_char_t ch, int base = 10)
		{
			return _isalpha(ch) || _isdigit(ch, base);
		}

		_char_t _look_nextch()const { return _next_ch; }
		void _get_nextch()
		{
			if (_is_abort)return;
			_cur_ch = _next_ch;
			_next_ch = _getch_func();
			if (_cur_ch == '\n')
			{
				_cur_pos.line++;
				_cur_pos.column = 0;
			}
			else _cur_pos.column++;
		}
		bool _match_ch(_char_t want)
		{
			return _look_nextch() == want ? _get_nextch(), true : false;
		}

		inline bool _is_end()const { return !bool(_cur_ch); }

		void _skip_space()
		{
			while (isspace(_cur_ch))
			{
				_get_nextch();
			}
		}
		void _skip_line()
		{
			while (_cur_ch != end_flag)
			{
				if (_cur_ch == '\n')
				{
					_get_nextch();
					break;
				}
				_get_nextch();
			}
		}
		void _skip_comment()
		{
			if (_match_ch('/')) // 单行注释
			{
				_skip_line();
			}
			else if (_match_ch('*'))
			{
				_get_nextch();
				bool comment_closed = false;
				while (_cur_ch != end_flag)
				{
					_cur_node_start_pos = _cur_pos;
					if (_cur_ch == '*')
					{
						if (_match_ch('/'))
						{
							_get_nextch();
							comment_closed = true;
							break;
						}
					}
					_get_nextch();
				}
				if (!comment_closed)
				{
					// 错误 注释未闭合
					_throw_err(_origin::parse_comment, _error::item_not_closed);
				}
			}
		}

		_json_t _parse_num()
		{
			bool is_float = false;
			_str_t buf;

			if (_cur_ch == '-' || _cur_ch == '+')
			{
				buf.push_back(_cur_ch);
				_get_nextch();
			}
			while (true)
			{
				while (_isdigit(_cur_ch))
				{
					buf.push_back(_cur_ch);
					_get_nextch();
				}
				if (_cur_ch == '.')
				{
					buf.push_back(_cur_ch); _get_nextch();
					is_float = true;
					continue;
				}
				else if (_cur_ch == 'e' || _cur_ch == 'E')
				{
					buf.push_back(_cur_ch); _get_nextch();
					if (_cur_ch == '-' || _cur_ch == '+')
					{
						buf.push_back(_cur_ch); _get_nextch();
					}
					is_float = true;
					continue;
				}
				break;
			}

			if (is_float)
			{
				return _json_t(std::stod(buf));
			}
			else
			{
				long long n = std::stoll(buf);
				return (n <= std::numeric_limits<int>::max())
					? _json_t(static_cast<int>(n))
					: _json_t(n);

			}
		}

		void _parse_unicode_to(_str_t& s)
		{
			uint32_t idx = 0, val = 0;
			uint8_t digit = 0;

			_str_t buf = "\\u";

			while (idx++ < 4)
			{
				_get_nextch();
				_cur_node_start_pos = _cur_pos;
				buf.push_back(_cur_ch);
				if (_is_end())
				{
					// 错误 字符串未闭合
					_throw_err(_origin::parse_string, _error::item_not_closed);
					break;
				}
				else if ('0' <= _cur_ch && _cur_ch <= '9')
				{
					digit = _cur_ch - '0';
				}
				else if ('a' <= _cur_ch && _cur_ch <= 'f')
				{
					digit = _cur_ch - 'a' + 10;
				}
				else if ('A' <= _cur_ch && _cur_ch <= 'F')
				{
					digit = _cur_ch - 'A' + 10;
				}
				else
				{
					// 错误 非法的 unicode 转义
					_throw_err(_origin::parse_unicode, _error::illegal_escape, buf);
					break;
				}
				val = val * 16 | digit;
			}

			auto need = _sjson_detail::utf8::get_byte_num_of_encode(val);

			if (need == 0)
			{
				// 错误 无效的 unicode
				_throw_err(_origin::parse_unicode, _error::invalid_unicode_code, buf);
				return;
			}

			s.resize(s.size() + need);

			_sjson_detail::utf8::encode((uint8_t*)s.data() + s.size() - need, val);
		}

		_json_t _parse_string()
		{
			_str_t buf;
			while (true)
			{
				_cur_node_start_pos = _cur_pos;
				if (_is_end())
				{
					// 错误 字符串未闭合
					_throw_err(_origin::parse_string, _error::item_not_closed);
					break;
				}
				else if (_cur_ch == '"')
				{
					_get_nextch();
					break;
				}
				else if (_cur_ch == '\\')
				{
					_get_nextch();
					char ch = 0;
					switch (_cur_ch)
					{
					case '0':break;
					case 'a':ch = '\a'; break;
					case 'b':ch = '\b'; break;
					case 'f':ch = '\f'; break;
					case 'n':ch = '\n'; break;
					case 'r':ch = '\r'; break;
					case 't':ch = '\t'; break;
					case '"':ch = '\"'; break;
					case '\\':ch = '\\'; break;
					case 'u':
					{
						// _get_nextch() 在 _parse_unicode_to 处进行
						_parse_unicode_to(buf);
						_get_nextch();
						continue;
						break;
					}
					default:
						ch = _cur_ch;
						_throw_err(
							_origin::parse_string, _error::illegal_escape,
							{ '\\',ch }
						);
						break;
					}
					buf.push_back(ch);
				}
				else
				{
					buf.push_back(_cur_ch);
				}
				_get_nextch();
			}
			return buf;
		}

		_json_t _parse_keyword()
		{
			_cur_node_start_pos = _cur_pos;
			_str_t buf;
			buf.push_back(_cur_ch);
			_get_nextch();
			while (_isalnum(_cur_ch) || _cur_ch == '_')
			{
				buf.push_back(_cur_ch);
				_get_nextch();
			}

			if (buf == "true")return true;
			else if (buf == "false")return false;
			else if (buf == "null")return nullptr;

			// 错误 未知的关键词 
			_throw_err(_origin::parse_keyword, _error::unknown_keyword, buf);

			return nullptr;
		}

		_json_t _parse_simple_node()
		{
			if(_is_end()) return parser_delimiter(end_flag);
			while (!_is_end())
			{
				_skip_space();
				if (_is_end())return parser_delimiter(end_flag);
				switch (_cur_ch)
				{
				case '/':
					if (_look_nextch() == '/' || _look_nextch() == '*')
					{
						_skip_comment();
						continue;
					}
					else
					{
						// 错误 意外的标识符
						_throw_err(
							_origin::parse_comment,
							_error::unexpected_item,
							{ '{','[','/','\\','*',']','}','@',_look_nextch(),'\0' }
						);
					}
					break;
				case '"':
				{
					_get_nextch();
					return  _parse_string();
					break;
				}
				case ',': case ':': case '[': case ']':	case '{': case '}':
				{
					_char_t tmp = _cur_ch;
					_get_nextch();
					return static_cast<parser_delimiter>(tmp);
				}
				default:

					if (_isdigit(_cur_ch) || _cur_ch == '-' || _cur_ch == '+')
					{
						return _parse_num();
					}
					else if (_isalpha(_cur_ch))
					{
						return _parse_keyword();
					}
					_throw_err(
						_origin::parse_delimiter,
						_error::unexpected_item,
						std::string("{[/\",:\\[\\]\\{\\}}@") + _cur_ch
					);
					_get_nextch();
					return static_cast<parser_delimiter>(_cur_ch);
					break;
				}
				break;
			}
			return nullptr;
		}

		void _get_next_simple_node()
		{
			_skip_space();
			_cur_node_start_pos = _cur_pos;
			_cur_node = _parse_simple_node();
		}

		_json_t _parse_node()
		{
			_json_t j = _parse_simple_node();
			auto delimiter = j.get_if<parser_delimiter>();
			if (delimiter)
			{
				if (*delimiter == parser_delimiter::left_bracket)
				{
					return _parse_array();
				}
				else if (*delimiter == parser_delimiter::left_brace)
				{
					return _parse_object();
				}

			}
			return j;
		}

		void _get_next_node()
		{
			_skip_space();
			// _cur_node_start_pos 在 _get_next_simple_node 处更新
			_cur_node = _parse_node();
		}


		_json_t _parse_array()
		{
			_json_t res(json_value_t::array);
			_get_next_node();

			while (_cur_node != parser_delimiter::right_bracket && !_is_end())
			{
				if (_cur_node.hold<parser_delimiter>())
				{
					// 错误 期望值
					_throw_err(
						_origin::parse_array,
						_error::unexpected_item,
						"<!delimiter>@" + _cur_node.dump()
					);
					_cur_node = nullptr;
				}
				res.push_back(_cur_node);
				_get_next_simple_node();
				if (_cur_node == parser_delimiter::right_bracket)break;
				if (_cur_node != parser_delimiter::comma)
				{
					// 错误 期望 ','
					_throw_err(
						_origin::parse_array,
						_error::unexpected_item,
						"{,}@" + _cur_node.dump()
					);
					break;
				}
				_get_next_node();
			}
			if (_cur_node != parser_delimiter::right_bracket)
			{
				_throw_err(_origin::parse_array, _error::item_not_closed);
			}
			return res;
		}

		_json_t _parse_object()
		{
			_json_t res(json_value_t::object);
			_get_next_simple_node();

			_str_t key;

			while (_cur_node != parser_delimiter::right_brace && !_is_end())
			{
				if (!_cur_node.hold<_str_t>())
				{
					// 错误 期望 <string>
					_throw_err(
						_origin::parse_object,
						_error::unexpected_item,
						"<string>@" + _cur_node.dump()
					);
					_get_next_simple_node();
					continue;
				}
				key = _cur_node.get<_str_t>();
				_get_next_simple_node();
				if (_cur_node != parser_delimiter::colon)
				{
					// 错误 期望 ':'
					_throw_err(
						_origin::parse_object,
						_error::unexpected_item,
						"{:}@" + _cur_node.dump()
					);
				}
				_get_next_node();
				if (_cur_node.hold<parser_delimiter>())
				{
					// 错误 期望值
					_throw_err(
						_origin::parse_object,
						_error::unexpected_item,
						"<!delimiter>@" + _cur_node.dump()
					);
					_cur_node = nullptr;
				}
				res[key] = _cur_node;
				_get_next_simple_node();
				if (_cur_node == parser_delimiter::right_brace)break;
				if (_cur_node != parser_delimiter::comma)
				{
					// 错误 期望 ','
					_throw_err(
						_origin::parse_object,
						_error::unexpected_item,
						"{,}@" + _cur_node.dump()
					);
				}
				_get_next_simple_node();

			}

			if (_cur_node != parser_delimiter::right_brace)
			{
				_throw_err(_origin::parse_object, _error::item_not_closed);
			}

			return res;
		}



	public:

		void parse(size_t maxn = 0)
		{
			_json_t j;
			_get_next_node();

			if (maxn != 1) // 如果只读一个就不跳过空白（从 cin 读入完一个后不会继续等待）
				_skip_space(); // 防止结束后有空白导致 _is_end() 返回 false
			if (_is_end() || maxn == 1)
			{
				j.assign(_cur_node);
			}
			else
			{
				j.assign(json_value_t::array);
				j.push_back(_cur_node);

				while (!_is_end())
				{
					if (maxn)
					{
						if (j.size() == maxn)break;
					}

					_get_next_node();
					if (_cur_node.hold<parser_delimiter>())continue;
					j.push_back(_cur_node);
				}
			}
			_cur_node = j;
		}

		template<typename _iter_t>
		parser(_iter_t beg, _iter_t end, const json_parse_error_callback_f& f= defult_parse_err_callback)
		{
			_err_callback = f;
			_getch_func = [&beg, end]()
			{
				if (beg == end)return end_flag;
				_iter_t tmp = beg;
				++beg;
				return *tmp;
			};
			_next_ch = _getch_func();
			_get_nextch();
			parse();
		}

		parser(std::istream& is, const json_parse_error_callback_f& f= defult_parse_err_callback)
		{
			_err_callback = f;
			_getch_func = [&is]() {return _char_t(is.get()); };
			_next_ch = _getch_func();
			_get_nextch();
			parse(1);
		}

		void get_result_to(_json_t& out)const
		{
			out = _cur_node;
		}
		const _json_t& result()const
		{
			return _cur_node;
		}
	};

	json_callback_ret defult_parse_err_callback(
		uint32_t line, uint32_t column,
		json_error_origin origin,
		json_parse_error e, const std::string& msg
	)
	{
		std::stringstream ss;
		ss << "parser`<json_source>:" << line << '`' << column << ':';
		switch (origin)
		{
		case sjson::json_error_origin::parse_comment:
			ss << "parse_comment";
			break;
		case sjson::json_error_origin::parse_string:
			ss << "parse_string";
			break;
		case sjson::json_error_origin::parse_unicode:
			ss << "parse_unicode";
			break;
		case sjson::json_error_origin::parse_keyword:
			ss << "parse_keyword";
			break;
		case sjson::json_error_origin::parse_delimiter:
			ss << "parse_delimiter";
			break;
		case sjson::json_error_origin::parse_array:
			ss << "parse_array";
			break;
		case sjson::json_error_origin::parse_object:
			ss << "parse_object";
			break;
		default:
			break;
		}
		ss << ": ";
		switch (e)
		{
		case sjson::json_parse_error::item_not_closed:
			ss << "item_not_closed";
			break;
		case sjson::json_parse_error::illegal_escape:
			ss << "illegal_escape";

			break;
		case sjson::json_parse_error::invalid_unicode_code:
			ss << "invalid_unicode_code";

			break;
		case sjson::json_parse_error::unknown_keyword:
			ss << "unknown_keyword";

			break;
		case sjson::json_parse_error::unexpected_item:
			ss << "unexpected_item";

			break;
		default:
			break;
		}

		ss << ' ' << msg << '\n';

		std::cout << ss.str();

		return json_callback_ret::ignore_and_continue;
	}
};

using json = _basic_json<>;

inline json operator "" _json(const char* s, size_t n)
{
	return _sjson_detail::parser<json>(s, s + n).result();
}

};



#undef _JSON_THROW_TYPE_ADJUST
#undef _JSON_THROW_TYPE_ADJUST_RAW

#undef _JSON_ENSURE_IS

#undef _JSON_THROW
#undef _JSON_TRY
#undef _JSON_TRY_
