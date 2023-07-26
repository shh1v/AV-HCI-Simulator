class ExperimentHelper:
    @staticmethod
    def set_synchronous_mode(world):
        settings = world.get_settings()
        settings.synchronous_mode = True
        settings.fixed_delta_seconds = 0.025
        world.apply_settings(settings)

    @staticmethod
    def set_asynchronous_mode(world):
        settings = world.get_settings()
        settings.synchronous_mode = False
        settings.fixed_delta_seconds = None 
        world.apply_settings(settings)
    
    def sleep(world, time):
        start_time = time.time()
        while (time.time() - start_time) < time:
            world.tick()