#include <AppBase.h>

namespace apemode {
    class App : public apemode::AppBase {
        friend struct AppContent;

        std::unique_ptr< AppContent > content;
        float                         totalSecs;

    public:
        App( );
        virtual ~App( );

    public:
        virtual bool Initialize( int Args, char* ppArgs[] ) override;
        virtual apemode::IAppSurface* CreateAppSurface( ) override;

    public:
        virtual void OnFrameMove( ) override;
        virtual void Update( float DeltaSecs, apemode::Input const& InputState ) override;
        virtual bool IsRunning( ) override;
    };
}