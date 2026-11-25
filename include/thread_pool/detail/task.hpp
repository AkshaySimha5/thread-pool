#pragma once

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace thread_pool::detail {

/**
 * @brief A move-only, type-erased wrapper around any `void()` callable.
 *
 * Why not std::function<void()>?
 *   std::function requires its target to be CopyConstructible (until
 *   C++23's std::move_only_function). std::packaged_task - which is what
 *   gives submit() its future - is move-only, so it cannot be stored in
 *   a std::function. Task fixes that.
 *
 * How the type erasure works:
 *   Task itself is not a template, so a std::queue<Task> can hold tasks
 *   wrapping completely different callable types. Internally it owns a
 *   pointer to an abstract `Concept` (virtual run()). For each concrete
 *   callable type F the constructor instantiates a `Model<F>` that
 *   derives from Concept and stores the F. The virtual call is the
 *   "erasure": the caller only sees Concept, the callable's real type
 *   lives only inside Model<F>.
 *
 * Cost: one heap allocation per Task (the Model) plus one virtual call.
 *   A small-buffer optimisation (store small callables inline) is a good
 *   later benchmark-driven improvement;
 */
class Task {
public:
    Task() noexcept = default;
    // empty task with impl_ == nullptr

    // Constrained so that:
    //  - Task(Task&) / Task(const Task&) never hijack the copy/move ctors
    //    (the decay check: a forwarding ctor would otherwise be a better
    //    match for a non-const lvalue Task than the deleted copy ctor)
    //  - only callables invocable as void() are accepted.
    template <typename F>
        requires(!std::same_as<std::decay_t<F>, Task> &&
                 std::invocable<std::decay_t<F>&>)
                 // Ensures at compile time that the decayed callable can actually be invoked with zero arguments (fn())
                 // If a function requiring arguments is passed, compile error immediately rather than an Model::run()
    explicit Task(F&& f)
        : impl_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))) {}
    // prevents accidental implicit conversions of lambdas or functions into heap-allocating Task objects

    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;
    // moving simply transfers ownership of impl_(pointer swap), fast and guaranteed not to throw

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    // std::unique_ptr cannot be copied, reinforcing Task is move-only

    ~Task() = default;

    /// Invoke the stored callable. Precondition: *this is non-empty.
    void operator()() { impl_->run(); }
    // makes Task a callable object(functor)
    // worker simply calls task() which delegates to impl_->run()

    /// True if a callable is stored.
    explicit operator bool() const noexcept { return impl_ != nullptr; }
    // safe boolean checks to verify Task is not empty before calling it
    // prevents implicit int or bool conversions

private:
    struct Concept {
        virtual ~Concept() = default;
        // deleting a derived object through a base class pointer (Concept*) and if the base class destructor is not virtual, the behavior is undefined
        // Deleting through Concept* performs a virtual dispatch to ~Model<F>(), which destroys fn properly before freeing the Model<F> memory block.
        virtual void run() = 0;
        // Concept is an abstract interface, cannot be instantiated directly
        // Derived class(Model<F>) should implement run()
        // bridges the gap between compile-time Model<F> and runtime polymorphism(Concept*)
    };

    template <typename F>
    struct Model final : Concept {
        // final: Tells the compiler that no class will ever inherit from Model<F>. 
        // While Task still calls impl_->run() through a base Concept* pointer (requiring a virtual lookup), 
        // marking Model as final allows the compiler to optimize any internal calls within Model itself without virtual dispatch.
        template <typename U>
        explicit Model(U&& u) : fn(std::forward<U>(u)) {}

        void run() override { fn(); }  // return value, if any, is discarded

        // Because impl_->run() resolves the target address at runtime via pointer chasing (impl_ -> vptr -> function address), 
        // the compiler generally cannot inline fn() into the worker thread's loop. 
        // negligible effects

        F fn;
    };

    std::unique_ptr<Concept> impl_;
};

}  // namespace thread_pool::detail