#ifndef QDV_RESULT_H
#define QDV_RESULT_H

#include <QString>
#include <optional>
#include <utility>

namespace QDV {

template <typename T>
class Result {
public:
    static Result<T> ok(T value) {
        return Result(true, std::move(value), {});
    }

    static Result<T> err(const QString& error) {
        return Result(false, T{}, error);
    }

    bool isOk() const { return m_ok; }
    bool isErr() const { return !m_ok; }

    explicit operator bool() const { return m_ok; }

    const T& value() const { return m_value; }
    T& value() { return m_value; }

    const T& unwrap() const { return m_value; }

    const T& unwrapOr(const T& defaultValue) const {
        return m_ok ? m_value : defaultValue;
    }

    const QString& error() const { return m_error; }

    template <typename U>
    Result<U> map(std::function<U(const T&)> fn) const {
        if (m_ok) {
            return Result<U>::ok(fn(m_value));
        }
        return Result<U>::err(m_error);
    }

    template <typename U>
    Result<U> andThen(std::function<Result<U>(const T&)> fn) const {
        if (m_ok) {
            return fn(m_value);
        }
        return Result<U>::err(m_error);
    }

private:
    Result(bool ok, T value, QString error)
        : m_ok(ok), m_value(std::move(value)), m_error(std::move(error)) {}

    bool m_ok;
    T m_value;
    QString m_error;
};

template <>
class Result<void> {
public:
    static Result<void> ok() {
        return Result(true, {});
    }

    static Result<void> err(const QString& error) {
        return Result(false, error);
    }

    bool isOk() const { return m_ok; }
    bool isErr() const { return !m_ok; }
    explicit operator bool() const { return m_ok; }

    const QString& error() const { return m_error; }

private:
    Result(bool ok, QString error)
        : m_ok(ok), m_error(std::move(error)) {}

    bool m_ok;
    QString m_error;
};

} // namespace QDV

#endif // QDV_RESULT_H