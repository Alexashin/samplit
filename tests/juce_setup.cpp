#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_events/juce_events.h>

// MessageManager должен появиться в главном потоке до того, как процессор запустит таймеры и фоновую загрузку
class JuceSetup final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting (const Catch::TestRunInfo&) override { juce::initialiseJuce_GUI(); }
    void testRunEnded (const Catch::TestRunStats&) override { juce::shutdownJuce_GUI(); }
};

CATCH_REGISTER_LISTENER (JuceSetup)
