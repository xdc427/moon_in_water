import os

user_dirs = [ 'user_code/x_shadow1', 'user_code/x_shadow2' ]
user_libs = []

test_env = Environment( ENV = os.environ, CCFLAGS=[ '-DLEVEL_NORMAL', '-DLEVEL_TEST' ], LIBS='pthread', MOON_TARGET='test', CPPPATH='#./moon_code/include_test', ANALYSE_DIRS=[ 'test_code' ] )
release_env = Environment( ENV = os.environ, CCFLAGS='-DLEVEL_NORMAL', LIBS='pthread', MOON_TARGET='release', CPPPATH='#./moon_code/include_release', ANALYSE_DIRS=user_dirs )


env = test_env.Clone();
Export( 'env' )
moon_lib = SConscript( './moon_code/SConstruct')
env.Append( CCFLAGS = '-DMOON_ID=%s' % ( 'test_code' ) )
test_lib = SConscript( './test_code/SConstruct' )
env.Program( 'test', [ test_lib ] )

if len( user_dirs ) > 0 :
	env = release_env.Clone()
	Export( 'env' )
	moon_lib = SConscript( './moon_code/SConstruct' ) 
	env.Append( CCFLAGS = [ '-DMOON_ID' ] )
	for dir in user_dirs :
		env = env.Clone()
		cflags = env['CCFLAGS'];
		for i in range( 0, len( cflags ) ) :
			if cflags[i].startswith( '-DMOON_ID' ) :
				cflags[i] = '-DMOON_ID=%s' % ( dir.split('/')[-1] ) 
		env.Replace( CCFLAGS = cflags )
		Export( 'env' )
		user_lib = SConscript( './%s/SConstruct' % ( dir ) )
		env.Program( dir.split('/')[-1], [ user_lib ] )
#		user_libs.append( user_lib )
#	env.Program( 'connect', [ user_libs, moon_lib ] )
