# Concepts

C++20 introduces [`concepts`](https://en.cppreference.com/w/cpp/language/constraints), which are essentially a way to express constraints and requirements for templates.

> Class templates, function templates, and non-template functions (typically members of class templates) may be associated with a constraint, which specifies the requirements on template arguments, which can be used to select the most appropriate function overloads and template specializations.
>
> Named sets of such requirements are called concepts. Each concept is a predicate, evaluated at compile time, and becomes a part of the interface of a template where it is used as a constraint.

The goal of this new feature is to improve the readability of code and reduce the reliance on the often confusing technique of SFINAE (Substitution Failure Is Not An Error).

Before concepts, it was difficult to understand exactly what was required of a template type. This resulted in either overly broad constraints or excessive template metaprgramming with cryptic compiler error messages. Concepts solve this problem by allowing the programmer to specify exactly what is required of the template type and simplified compiler errors that usually tell the programmer exactly what requirement of the concept was incorrect.

## Concept Patterns

Let's start with a common set of concepts used in MRC:

```c++
template <typename T>
concept has_data_type = std::movable<typename T::data_type>;

template <typename T, typename DataT>
concept has_data_type_of = has_data_type<T> && std::same_as<typename T::data_type, DataT>;

template <typename T, auto ConceptFn>
concept has_data_type_of_concept = has_data_type<T> && eval_concept_fn_v<ConceptFn, typename T::data_type>;
```

### Pattern #1: `_of`

The `has_data_type` concept checks if a type `T` has a nested type `data_type` that is movable. The `has_data_type_of` concept extends `has_data_type` and also checks if the `T::data_type` is the same as `DataT`.

Extending a concept with `_of` is a common pattern we will use in MRC that will be synonymous evaluting the concept plus `std::same_as` for a particular type. Here is an example:

```c++
template<typename T>
class Foo
{
  public:
    using data_type = T;

    Foo(T data) : m_data(std::move(data)) {}

    const T& data() const { return m_data; }

  private:
    T m_data;
};

template <typename T>
class Bar
{
  public:
    using data_type = T;

    Bar(T data) : m_data(std::move(data)) {}

    const T& data() const
    {
        return m_data;
    }

  private:
    T m_data;
};

template<typename T>
concept fooable = has_data_type<T> && requires(T t) {
    { t.data() } -> std::same_as<const typename T::data_type&>;
};

template<typename T, typename DataT>
concept fooable_of = fooable<T> && has_data_type_of<T, DataT>;

static_assert(fooable<Foo<int>>);
static_assert(fooable<Bar<float>>);

static_assert(fooable_of<Foo<int>, int>);
static_assert(fooable_of<Bar<float>, float>);
```

We can then use `fooable_of<int>` as a type requirement and call `bar(f.data())` knowing that our type requirements guarantee an `int` compatible `foo`.

```c++
template<fooable_of<int> FooT>
void fooable_of_int_fn(FooT& f) {
    auto bar = [](const int& data) { return data * 2;};
    std::cout << "ans: " << bar(f.data()) << std::endl;
}
```

### Pattern #2 : `of_concept`

Using `has_data_type_of` allows for `fooable_of`, but both take a general concept and make it much more concrete. To dial back the concreteness, we have the second reoccurring pattern `of_concept`. This pattern allows to us to have a `fooable_of<T>` where `T` is not a concrete type, but rather a concept.

```c++
template<typename T>
concept arithmetic = std::is_arithmetic_v<T>;

template<typename T, auto ConceptFn>
concept fooable_of_concept = fooable<T> && has_data_type_of_concept<T, ConceptFn>;

template<typename T>
concept fooable_of_arithmetic = fooable_of_concept<T, MRC_CONCEPT(arithmetic)>;

template<fooable_of_arithmetic FooT>
void fooable_of_arithmetic_fn(FooT& f) {
    // lambda that take any data_type that is arithmetic
    auto bar = [](arithmetic auto const& data) { return data * 2;};
    std::cout << "ans: " << bar(f.data()) << std::endl;
}
```

Unfortunately, C++ concepts can not take template parameters that are concepts. To get `of_concept` to take a general concept, we have to wrap the concept in a `consteval` templated lambda. The `MRC_CONCEPT` macro provides this utility.

```c++
#define MRC_CONCEPT(concrete_concept) []<typename _T>() consteval { return concrete_concept<_T>; }
```

Now, we have a `fooable_of_arithmetic_fn` which can take a `Foo<int>` or a `Foo<double>` but not a `Foo<std::string>`.

### Pattern #3: `of_concept_of`

The final pattern is `of_concept_of`. One use case for this pattern is to allow us to use the `of` pattern for a set of concrete types applied to a set of `of_concept` elements.

To demonstrate this pattern we want a tuple of `fooable`s such that the first element the tuple is a `fooable_of<int>`, then second a `fooable_of<double>` and the third a `fooable_of<std::string>`.

```c++
Foo<int> foo_int(21);
Bar<float> bar_flt(1.675);
Foo<std::string> foo_str("mrc");

auto t = std::make_tuple(foo_int, bar_flt, foo_str);

static_assert(tuple_of_concept_of<decltype(t), MRC_CONCEPT_OF(fooable_of), int, float, std::string>);
```

## Examples

```c++
Foo<int> foo_int(21);
Bar<float> bar_flt(1.675);
Foo<std::string> foo_str("mrc");

EXPECT_EQ(fooable_of_int_fn(foo_int), 42);
// fooable_of_int_fn(bar_flt);
// fooable_of_int_fn(foo_str);

static_assert(fooable_of_arithmetic<Foo<int>>);
static_assert(fooable_of_arithmetic<Foo<double>>);
static_assert(!fooable_of_arithmetic<Foo<std::string>>);

EXPECT_EQ(fooable_of_arithmetic_fn(foo_int), 42);
EXPECT_FLOAT_EQ(fooable_of_arithmetic_fn(bar_flt), 3.14);
// fooable_of_arithmetic_fn(foo_str);

auto t = std::make_tuple(foo_int, bar_flt, foo_str);
static_assert(tuple_of_concept_of<decltype(t), MRC_CONCEPT_OF(fooable_of), int, float, std::string>);
```
